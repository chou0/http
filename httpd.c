#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>  
#include <sys/epoll.h>
#include <signal.h>
#include "httpd.h"
#include "mlanguage.h"
#include "mlog.h"

#define HTTP_PORT 80
#define MAX_CONNECT 20

#define SERVER_STRING "Server: httpd/0.1.0\r\n"

extern int errno;

long setnoblock(int fd)
{
    int opts;
    
    opts = fcntl(fd, F_GETFL);
    if (opts < 0) {
        perror("fcntl(F_GETFL)\n");
        return -1;
    }
    
    opts = (opts | O_NONBLOCK);
    if (fcntl(fd, F_SETFL, opts) < 0) {
        perror("fcntl(F_SETFL)\n");
        return -2;
    }
    
    return 0;        
}

long start_up(int *httpd)
{
	struct sockaddr_in server;
	socklen_t len;
	int ret;

	if (0 > (*httpd = socket(AF_INET,SOCK_STREAM, 0)))
	{
		printf("create socker err\n");
		return -1;
	} 	
	
    // 获取sockClient1对应的内核接收缓冲区大小  
    int optVal = 0;  
    int optLen = sizeof(optVal);  
    getsockopt(*httpd, SOL_SOCKET, SO_RCVBUF, (char*)&optVal, &optLen);  
    printf("sockClient1, recv buf is %d\n", optVal); // 8192 
    
    //optVal = 0;  
    //setsockopt(*httpd, SOL_SOCKET, SO_RCVBUF, (char*)&optVal, optLen); 
    
    optVal = 1;
    ret = setsockopt( *httpd, SOL_SOCKET, SO_REUSEADDR, (char*)&optVal, sizeof(optVal) );
    
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;//300*1000;

    //接受时限
    setsockopt(*httpd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout,sizeof(timeout));
    
    setnoblock(*httpd);
    
    bzero(&server,sizeof server);
	server.sin_family = AF_INET;
	server.sin_port = htons(HTTP_PORT);
    server.sin_addr.s_addr = htonl(INADDR_ANY);
	len = sizeof(server);

	if (-1 == bind(*httpd,(struct sockaddr *)&server,len))
	{
		printf("bind err\n");
		goto fail_label;
	}

	if (-1 == listen(*httpd,MAX_CONNECT))
	{
		printf("listen err\n");
		goto fail_label;
	}
 	
	ret = 0;
    return ret;
fail_label:
	close(*httpd);
	ret = -1;
	return ret;	
}

struct request *request_parse(char *buffer, long len)
{
    struct request *req;
    char *p = buffer;
    int i;
    
    if (NULL == (req = malloc(sizeof *req)))
    {
        printf("malloc failed\n");
        return NULL;
    }
    
    sscanf(p, "%s%n", req->methon, &i);
    if (i > 7
        || i <= 0) 
    {
        printf("get methon err\n");
        goto fail;
    }
    req->methon[i] = 0;
    printf("methon : %s \n",req->methon);
    
    p += i;
    p ++;
    sscanf(p, "%s%n", req->url, &i);
    if (i > 200
        || i <= 0) 
    {
        printf("get url err\n");
        goto fail;
    }
    req->url[i] = 0;
    printf("url : %s \n",req->url);
  
    return req;
fail:
    free(req);
    return NULL;
}

void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

long execute_cgi(char *url, int client)
{
    char c,buf[100];
    int i;
    
    if (access(url, F_OK) != F_OK)
    {
        not_found(client);
        return 0;   
    }
    
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    send(client, "Content-type: text/html\r\n", strlen("Content-type: text/html\r\n"), 0);
    
    sprintf(buf, "Content-length: %d\r\n\r\n", 5);
    send(client, buf, strlen(buf), 0);
    
    strcpy(buf, "hello");
    send(client, buf, strlen(buf), 0);
        
    return 0; 
}

long send_file(char *url, int client) 
{
    char buffer[1024], *p = NULL;
    int len, ret;
    struct stat filestat;
    
    if (access(url, F_OK) != F_OK)
    {
        send(client, "HTTP/1.1 404 OK\r\n", strlen("HTTP/1.1 404 OK\r\n"), 0); 
        return 0;   
    }
    
    int filefd = open(url, O_RDONLY);
    stat(url, &filestat);
    len = filestat.st_size;
    
    send(client, "HTTP/1.1 200 OK\r\n", strlen("HTTP/1.1 200 OK\r\n"), 0);
    send(client, "Date: Wed May 24 09:43:45 CST 2017\r\n", strlen("Date: Wed May 24 09:43:45 CST 2017\r\n"), 0);
    
    if (NULL == (p = strrchr(url, '.')))
    {
        printf("err fail when get ends\n");
        close(filefd);
        return -1;    
    }
        
    if (strcmp(p, ".htm") == 0
        || strcmp(p, ".html") == 0)
    {
        send(client, "Content-type: text/html\r\n", strlen("Content-type: text/html\r\n"), 0);
    }
    else if (strcmp(p, ".ico") == 0)
    {
        send(client, "Content-type: img/ico\r\n", strlen("Content-type: img/ico\r\n"), 0);
    }
    else if (strcmp(p, ".js") == 0) 
    {
        send(client, "Content-type: application/x-javascript\r\n", strlen("Content-type: application/x-javascript\r\n"), 0);
    }
    
    send(client, "Content-type: text/html\r\n", strlen("Content-type: text/html\r\n"), 0);
    send(client, "Content-Encoding: gzip\r\n", strlen("Content-Encoding: gzip\r\n"), 0);

    sprintf(buffer, "Content-length: %d\r\n\r\n", len);
    send(client, buffer, strlen(buffer), 0);

    while (len > 0)
    {
        ret = sendfile(client, filefd, NULL, len);
        if (ret < 0)
        {
            mlog("err : %s ---------", strerror(errno));        
        }
     
        len -= ret;
    }

    close(filefd);
    return 0;      
}

long do_request(struct request *req, int client)
{
    int cgi = 0;
    
    if (NULL == req)
    {
        printf("request is null\n");
        return -1;
    }
    
    if (memcmp(req->methon, "GET", 3) == 0)
    {
        if (strcmp(req->url, "/") == 0)
        {
            memcpy(req->url, "/index.htm", 15);
        }
    }
    else if (memcmp(req->methon, "POST", 4) == 0)
    {
        cgi = 1;
    }
    
    strcat(req->url, ".gz");
    printf("url: %s\n", req->url);
    if (cgi)
    {
        execute_cgi(req->url, client);    
    }
    else
    {
        send_file(req->url, client);
    }
        
    free(req);
    return 0;
}

void httpd_destory(int status, void *arg)
{
    int httpd = *(int *)arg;
    
    close(httpd);    
}

int recv_dump(int fd)
{
    FILE *fp;
    char buf[1024];
    
    if (NULL == (fp = fdopen(fd, "r+")))
    {
        printf("failed when fdopen\n");
        goto failed_label;
    }
    
    while (fgets(buf, sizeof(buf), fp) != NULL)
    {
        printf("-------------------------");
        printf("%s", buf);
    }
    
    fclose(fp);
    return 0;
    
failed_label:
    close(fd);
    return -1;
}

int main(int argc,char ** args)
{
    int httpd,client;
	struct sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);
	int epfd,nfds, i, fd, n, nread;
    struct epoll_event ev,events[MAX_CONNECT];
    char buf[2048];
    
    signal(SIGPIPE, SIG_IGN);
    //set_log_file("./log.txt");
    chroot("./web/");
    on_exit(httpd_destory, (void *)&httpd);
    bzero(&client_addr,sizeof client_addr);
    	
	if (start_up(&httpd))
	{
		printf("start up server err\n");
		return -1;
	}
       
    epfd = epoll_create(MAX_CONNECT);
    if (epfd == -1)
    {
        printf("err when epoll create\n");
        return -1;
    }
   
    ev.data.fd = httpd;
    ev.events = EPOLLIN|EPOLLET;//监听读状态同时设置ET模式
    epoll_ctl(epfd, EPOLL_CTL_ADD, httpd, &ev);//注册epoll事件
    while(1)
    {
        nfds = epoll_wait(epfd, events, MAX_CONNECT, -1);
        if (nfds == -1)
        {
            printf("failed when epoll wait\n");
            return -1;
        }
                
        for (i = 0; i < nfds; i++)
        {
            fd = events[i].data.fd;
            if (fd == httpd)
            {
                while ((client = accept(httpd, (struct sockaddr *) &client_addr, (size_t *)&len)) > 0) 
                {
                    if (setnoblock(client))
                    {
                        mlog("failed when set client nonlock");
                        continue;
                    }
                    
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = client;
                    
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client, &ev) == -1)
                    {
                        perror("epoll add client err");
                        continue;
                    }
                    
                    printf("accept fd : %d\n", client);
                }
                
                if (errno != EAGAIN && errno != ECONNABORTED 
                                && errno != EPROTO && errno != EINTR) 
                                    perror("accept");
                
                continue;
            }
            
            if (events[i].events & EPOLLIN) 
            {
 #if 0               
                n = 0;
                while ((nread = read(fd, buf + n, BUFSIZ-1)) > 0) //ET下可以读就一直读
                {
                    n += nread;
                }
                
                if (n == 0)
                {
                    mlog("client: %d request over", fd);
                    close(fd);
                    continue;
                }
                
                buf[n] = 0;        
                if (nread == -1 && errno != EAGAIN) 
                {
                    perror("read error");
                }
                        
                printf("read fd : %d\n", fd);
                printf("%s",buf);
#else
                recv_dump(fd);
#endif                
                
                    #if 0    
                        ev.data.fd = fd;
                        ev.events = events[i].events | EPOLLOUT; //MOD OUT 
                        if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == -1) {
                                perror("epoll_ctl: mod");
                        }
                   #else
                       //do_request(request_parse(buf, len), fd); 
                   #endif     
            }
 #if 0           
            if (events[i].events & EPOLLOUT) {
                        printf("write fd : %d\n", fd);
                        
                        sprintf(buf, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n{\"a\":\"1\",\"b\":\"2\"}", 17);
                        int nwrite, data_size = strlen(buf);
                        n = data_size;
                        while (n > 0) {
                                nwrite = write(fd, buf + data_size - n, n);//ET下一直将要写数据写完
                                if (nwrite < n) {
                                        if (nwrite == -1 && errno != EAGAIN) {
                                                perror("write error");
                                        }
                                        break;
                                }
                                n -= nwrite;
                        }
                        close(fd);
            }
#endif            
        }
    }
    
fail_label:
	close(httpd);
	return 0;
}

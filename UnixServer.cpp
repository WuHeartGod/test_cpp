#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <netinet/in.h>
#include <arpa/inet.h>


#include <fcntl.h>
#include <sys/wait.h>
#include <sys/epoll.h>


#include <vector>
#include <algorithm>
#include <list>
using namespace std;
 
 
typedef std::vector<struct epoll_event> EventList;

void milliseconds_sleep(unsigned long mSec){
    struct timeval tv;
    tv.tv_sec=mSec/1000;
    tv.tv_usec=(mSec%1000)*1000;
    int err;
    do{
       err=select(0,NULL,NULL,NULL,&tv);
    }while(err<0 && errno==EINTR);
}

class timetask
{
public:
    timetask(){}
    ~timetask(){}
    virtual void runtask()
    {
        printf("-------\n");
       // milliseconds_sleep(10);
    }
private:

};
class timeRunner
{
public:
    timeRunner(timetask* ptask,int type,int timeout)
    {
        m_ptask=ptask;
        m_itype=type;
        m_itimeout=timeout;
        m_iCycleTowait=0;
    }
    void SetCycle(int iCycle)
    {
        m_iCycleTowait=iCycle;
    }
    int GetCycle()
    {
        return m_iCycleTowait;
    }
    int GetTimeOut()
    {
        return m_itimeout;
    }
    int GetType()
    {
        return m_itype;   
    }

    void DecCycle()
    {
        m_iCycleTowait--;
    }
    void run()
    {
        m_ptask->runtask();
    }
    ~timeRunner()
    {
        delete m_ptask;
    }
private:
    timetask* m_ptask;
    int m_itype;
    int m_itimeout;
    int m_iCycleTowait;
};

typedef list<timeRunner*> RunnerList;

#define TIME_ONE 0
#define TIME_LOOP -1

class timewheel
{
public:
    timewheel(int ticks,int step_ms):m_ticks(ticks),m_current(0),m_step_ms(step_ms)
    {
        m_pTickArray=new RunnerList[ticks];
    }
    ~timewheel()
    {
        delete[] m_pTickArray;
    }
    int AddTimer(int type,int timeout_ms,timetask* ptask)
    {
        if(ptask==NULL)
        {
            return -1;
        }
        timeRunner* pRunner=new timeRunner(ptask,type,timeout_ms);

        int iTicks=(timeout_ms/m_step_ms);
        int iCycle=iTicks/m_ticks;
        pRunner->SetCycle(iCycle);
        int tick=(iTicks+m_current)%m_ticks;
        m_pTickArray[tick].push_back(pRunner);
    }
    void Run()
    {
        struct timeval tv_start;
        gettimeofday(&tv_start,NULL);
        int tick_run=0;
        while(1)
        {

            for(int i=0;i<m_ticks;++i)
            {
                tick_run++;
                
                m_current=i;
                timeRunner* pRunner=NULL;
                bool isHaveRunner=false;
                RunnerList::iterator it=m_pTickArray[i].begin();
                while(it!=m_pTickArray[i].end())
                {
                    //printf("");
                    if((*it)->GetCycle()==0)
                    {
                        (*it)->run();
                        if((*it)->GetType()==TIME_LOOP)
                        {
                            int timeout_ms=(*it)->GetTimeOut();
                            int iTicks=(timeout_ms/m_step_ms);
                            int iCycle=iTicks/m_ticks;
                            (*it)->SetCycle(iCycle);
                            int tick=(iTicks+m_current)%m_ticks;
                            if(tick!=i)
                            {
                                printf("timeout_ms %d  runner list from %d to %d,iCycle=%d\n",timeout_ms,i,tick,iCycle);
                                m_pTickArray[tick].push_back(*it);
                                m_pTickArray[i].erase(it++);
                                continue;
                            }
                            else
                            {
                                printf("timeout_ms %d  runner list from %d to %d,iCycle=%d\n",timeout_ms,i,i,iCycle);
                            }
                        }
                        else
                        {
                            m_pTickArray[i].erase(it++);
                            continue;
                        }
                    }
                    else
                    {
                        (*it)->DecCycle();
                    }
                    it++;
                }
                struct timeval tv_end;
                gettimeofday(&tv_end,NULL);
                double  timecost=(tv_end.tv_sec-tv_start.tv_sec)*1000.0+(tv_end.tv_usec-tv_start.tv_usec)/1000.0;
                double time_left=tick_run*m_step_ms*1.0-timecost;
                if(time_left>0)
                {
                    //printf("left=%f\n",time_left);
                    milliseconds_sleep(time_left);

                }
                else
                {
                    printf("-------left=%f\n",time_left);
                }
            }
        }
    }
private:
    RunnerList* m_pTickArray;
    int m_ticks;
    int m_step_ms;
    int m_current;
};


char gSocketPath[]="/home/socketfile/";

 int MyBind(int fd,char* name)
{
    int size,iband;
    struct sockaddr_un un;
    un.sun_family=AF_UNIX;
    snprintf(un.sun_path,sizeof(un.sun_path),"%s",name);
    unlink(un.sun_path);
    
    size=offsetof(struct sockaddr_un,sun_path)
        +strlen(un.sun_path);
    if((iband=bind(fd,(struct sockaddr*)&un,size))<0)
    {
        printf("bind:errno[%d]:%s\n",errno,strerror(errno));
        return -1;
    }

    return 0;
}
int GetSocketFd()
{
    int fd;
    if((fd=socket(AF_UNIX,SOCK_STREAM,0))<0)
    {
        printf("socket:errno[%d]:%s\n",errno,strerror(errno));
        return -1;
    }
    return fd;
}

#define ERR_EXIT(m) \
    do { \
        perror(m); \
        exit(EXIT_FAILURE); \
        } while (0)

void activate_nonblock(int fd)
{
	int ret;
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1)
		ERR_EXIT("fcntl error");
 
	flags |= O_NONBLOCK;
	ret = fcntl(fd, F_SETFL, flags);
	if (ret == -1)
		ERR_EXIT("fcntl error");

}
void deactivate_nonblock(int fd)
{
	int ret;
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1)
		ERR_EXIT("fcntl error");
 
	flags &= ~O_NONBLOCK;
	ret = fcntl(fd, F_SETFL, flags);
	if (ret == -1)
		ERR_EXIT("fcntl error");
}


class UnixStream
{
public:

    UnixStream(char* szName,bool bServer)
    {
        memset(m_szName,0,sizeof(m_szName));
        snprintf(m_szName,sizeof(m_szName)-1,"%s/%s.socket",gSocketPath,szName);
        m_bServer=bServer;
    }

    int InitSocket()
    {
        if((fd=GetSocketFd())<0)
        {
            return -1;
        }
        if(m_bServer)
        {
            int iret=MyBind(fd,m_szName);
            if(iret!=0)
            {
                return -1;
            }
            epollfd = epoll_create1(EPOLL_CLOEXEC); 
            return 0;
        }
        else
        {
            return 0;
        }
    }
    int Listen()
    {
        if(listen(fd,10)<0)
        {
            printf("listen:errno[%d]:%s\n",errno,strerror(errno));
            return -1;
        }
       
        return 0;
    }
    void Run()
    {

        static char recv_buf[10];

        struct sockaddr_un clt_addr;
        socklen_t len=sizeof(clt_addr);

        struct epoll_event eventlist[10];

        socklen_t peerlen;
        struct sockaddr_in peeraddr;


        struct epoll_event event;
        event.data.fd = fd;
        event.events = EPOLLIN | EPOLLET; //边沿触发
        epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

        int conn;
        int client_num=0;

        while(1)
        {
            int iready=epoll_wait(epollfd, eventlist,10,1000);
            if(iready==-1)
            {
                printf("epoll_wait:errno[%d]:%s\n",errno,strerror(errno));
                return ;
            }
            if (iready == 0&&EINTR==errno)
            {
                iready=0;
                printf("epoll_wait:errno[%d]:%s\n",errno,strerror(errno));
                continue;
            }
                
            for (int i = 0; i < iready; i++)
            {
                bool isClose=false;
                if (eventlist[i].data.fd == fd)
                {
                    peerlen = sizeof(peeraddr);
                    conn = accept(fd, (struct sockaddr *)&peeraddr, &peerlen);
                    if(conn<0)
                    {
                        printf("accept:errno[%d]:%s\n",errno,strerror(errno));
                        if (errno == EWOULDBLOCK)
                        {
                             continue;
                        }
                        else if (errno == EINTR || errno == EMFILE || errno == ECONNABORTED || errno == ENFILE ||
                                    errno == EPERM || errno == ENOBUFS || errno == ENOMEM)
                        {
                           // perror("accept");//! if too many open files occur, need to restart epoll event
                           // m_epoll->mod_fd(this);//重启
                             continue;
                        }
                    }

                    client_num++;
                    activate_nonblock(conn);
                    event.data.fd = conn;
                    event.events = EPOLLIN | EPOLLET;
                    epoll_ctl(epollfd, EPOLL_CTL_ADD, conn, &event);
                    printf("accept[%d]\n",conn);
                }
                else if(eventlist[i].events&(EPOLLIN|EPOLLPRI)/*EPOLLPRI带外数据*/)
                {
                    conn = eventlist[i].data.fd;
                    if (conn < 0)
                        continue;
                    
                    int num;
                    do
                    {
                        memset(recv_buf,0,sizeof(recv_buf));
                        num=read(conn,recv_buf,sizeof(recv_buf));
                        printf("Message from client[%d],len (%d)) :%s\n",conn,num,recv_buf);
                        if(num>0&&num<sizeof(recv_buf)-1)
                        {
                            break;
                        }
                        if(num==0)
                        {
                            client_num--;
                            close(conn);
                            event = eventlist[i];
                            epoll_ctl(epollfd, EPOLL_CTL_DEL, conn, &event);
                            isClose=true;
                            break; ;
                        }
                        else
                        {
                            printf("read:errno[%d]:%s\n",errno,strerror(errno));
                            if (errno == EINTR)
                            {
                                continue;
                            }
                            else if (errno == EWOULDBLOCK)
                            {
                                break;
                            }
                            // else
                            // {
                            //     printf("xxx:errno[%d]:%s\n",errno,strerror(errno));
                            //     client_num--;
                            //     close(conn);
                            //     event = eventlist[i];
                            //     epoll_ctl(epollfd, EPOLL_CTL_DEL, conn, &event);
                            //      isClose=true;
                            //      break; ;
                            // }   
                        }
                    }while(1);
                    
                    //int num=read(conn,recv_buf,sizeof(recv_buf));
                    //printf("Message from client[%d],len (%d)) :%s\n",conn,num,recv_buf);  
                    
                }
                if(isClose)
                {
                    continue;
                }
                if (eventlist[i].events& (EPOLLERR | EPOLLHUP))
                {
                   close(eventlist[i].data.fd);
                }
            }

        }

        // int com_fd=accept(fd,(struct sockaddr*)&clt_addr,&len);
        // if(com_fd<0)
        // {
        //     printf("accept:errno[%d]:%s\n",errno,strerror(errno));
        //     return ;
        // }
        // while(1)
        // {
            
        //     memset(recv_buf,0,1024);
        //     int num=read(com_fd,recv_buf,sizeof(recv_buf));
        //     printf("Message from client (%d)) :%s\n",num,recv_buf);  
        //     if(num<=0)
        //     {
        //         return ;
        //     }
        // }
        
    }
    int Connect(char* name)
    {
       struct sockaddr_un un;
        un.sun_family=AF_UNIX;
        snprintf(un.sun_path,sizeof(un.sun_path),"/%s/%s.socket",gSocketPath,name);


        int ret=connect(fd,(struct sockaddr*)&un,sizeof(un));
        if(ret==-1)
        {
            printf("connect:errno[%d]:%s\n",errno,strerror(errno));
            return -1;
        }
    }
    int Send(char* name,char* buff,int len)
    {
        int size;
        struct sockaddr_un un;
        un.sun_family=AF_UNIX;
        snprintf(un.sun_path,sizeof(un.sun_path),"/%s/%s.socket",gSocketPath,name);
        size=offsetof(struct sockaddr_un,sun_path)
            +strlen(un.sun_path);
        
        int iSend=write(fd,buff,len);
        return iSend;	   
    }
public:
    virtual ~UnixStream()
    {
        close(fd);
        unlink(m_szName);
    }
    
private:
    int fd;
    int epollfd;
    char m_szName[100];
    int m_bServer;
};

int main(int argc,char** argv)
{
    timewheel* pwheel=new timewheel(10,10);
    pwheel->AddTimer(TIME_LOOP,1020,new timetask);
    pwheel->AddTimer(TIME_LOOP,2150,new timetask);
    pwheel->AddTimer(TIME_LOOP,3050,new timetask);
    pwheel->AddTimer(TIME_LOOP,4070,new timetask);
    pwheel->Run();

    if(argc<2)
    {
        return -1;
    }
    
    if(argc==2)
    {
        UnixStream pUs(argv[1],true);
        if(pUs.InitSocket()!=0)
        {
            return -1;
        }
        if(pUs.Listen()<0)
        {
            return -1;
        }
        pUs.Run();
    }
    else
    {
        UnixStream pUs(argv[1],false);
        if(pUs.InitSocket()!=0)
        {
            return -1;
        }
        if(pUs.Connect(argv[2])!=0)
        {
            return -1;
        }
        char szBuff[]="dfgfhhhh";
        while(1)
        {
            if(argc>3)
            {
                int isend=pUs.Send(argv[2],argv[3],strlen(argv[3]));
                printf("send=%d\n",isend);
            }
            else
            {
                int isend=pUs.Send(argv[2],szBuff,sizeof(szBuff));
                printf("send=%d\n",isend);
            }
           
            sleep(1);
        }
        
    }
    return 0;
}
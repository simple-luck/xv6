#include "kernel/types.h"
#include "user.h"
#include "kernel/fs.h"
int main(int argc,char* argv[]){

    int p[2],p1[2];
    pipe(p);//定义第一个管道，用于父进程向子进程传送数据
    pipe(p1);//定义第二个管道，用于子进程向父进程传送数据
    int pid_child;
    char buf[54];
    if((pid_child=fork())<0){
        exit(1);
    }
    else if(pid_child==0){
        //子进程  p管道读，p1管道写
        //p1管道传子进程id
        close(p[1]);
        close(p1[0]);  

        //子进程先接收来自父进程的数据
        int nbuf=read(p[0],buf,sizeof(buf)-1);
         if (nbuf < 0) {
            fprintf(2, "Error reading from pipe\n");
            exit(1);
        }
        buf[nbuf]=0;
        printf("%d: received ping from pid %d\n",getpid(),atoi(buf));

        //再发送数据给父进程
        itoa(getpid(),buf);
        write(p1[1],buf,strlen(buf));
        
        close(p[0]);
        close(p1[1]);
        exit(0);
       
        

    }
    else{
        //父进程 p管道写，p1管道读
        //p管道传父进程id
        close(p[0]);
        close(p1[1]);

        //父进程先发送消息给子进程
        itoa(getpid(),buf);
        write(p[1],buf,strlen(buf));


        //从子进程中接收数据
        int nbuf=read(p1[0],buf,sizeof(buf)-1);
        if (nbuf < 0) {
            fprintf(2, "Error reading from pipe\n");
            exit(1);
        }
        buf[nbuf]=0;
        printf("%d: received pong from pid %d\n",getpid(),atoi(buf));
        
        close(p[1]);
        close(p1[0]);
        
        wait(0);
        exit(0);
        
    }
    exit(0);
}
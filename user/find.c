#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *path) {

  char *p;

  // Find first character after last slash.
  for (p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return name.
  return p;
}

void find(char * path,char *name){
    char buf[512], *p; 
    int fd;//文件描述符
    struct dirent de;//一个struct dirent结构体，用来保存从目录文件中读取的目录项
    struct stat st;//文件状态结构，存储文件的元数据，如文件类型

    if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
    }

    if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
    }

    switch (st.type) {
    case T_FILE:
      if(strcmp(fmtname(path),name)==0){
        //printf("%s %d %d %l\n", fmtname(path), st.type, st.ino, st.size);
        printf("%s",path);
      }
      break;

    case T_DIR:
      if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
        break;
      }
      strcpy(buf, path);
      p = buf + strlen(buf);
      *p++ = '/';
      while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;

        if (stat(buf, &st) < 0) {
          printf("find: cannot stat %s\n", buf);
          continue;
        }

        if(strcmp(fmtname(buf),name)==0){
          //printf("%s %d %d %d\n", fmtname(buf), st.type, st.ino, st.size);
          printf("%s\n",buf);
        }
        if(st.type==T_DIR&&strcmp(de.name,".")!=0&&strcmp(de.name,"..")!=0){
          find(buf,name);
        }

      }
      break;
  }
  close(fd);
}

int main(int argc,char* argv[]){
  if (argc < 3) {
    exit(0);
  }
  find(argv[1],argv[2]);
  exit(0);
}
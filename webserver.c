/*
Если взять очень обобщенно работу сервера, то получается такая последовательность действий:
Создать сокет
Привязать сокет к сетевому интерфейсу
Прослушивать сокет, привязанный к определенному сетевому интерфейсу
Принимать входящие соединения
Реагировать на события происходящие на сокетах
*/




#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>

const int BACKLOG = 10;


const char OK[] = "\
HTTP/1.1 200 Ok\r\n\
Connection: close\r\n\
Accept-Ranges:  none\r\n\
Content-Type: text/html; charset=UTF-8\r\n\r\n";

const char BEGIN[] = "<html><body style=\"font-family:Courier New\">";
const char END[] = "</body></html>";

int check_path (char path[]) {
    if (strstr(path, "..") != NULL) {
        printf("found .. in %s\n", path);
        return 0;
    }
    return 1;
}

void PrintInfo(char path[], char * buf) {
    struct stat s;
    if (lstat(path, &s) == -1) {
        char err[4096];
        strcpy(err, "directory: ");
        strcat(err, path);
        strcat(err, "; lstat");
        perror(err);
        //exit(1);
    }
    else {
        struct tm * mtime = localtime(&(s.st_mtime));
        if (mtime == NULL) {
            printf("localtime error\n");
			exit(1);
        }
        time_t now_ut;
        if (time(&now_ut) == -1) {
            printf("time error\n");
            exit(1);
        }
        struct tm * now = (struct tm *) malloc(sizeof(struct tm));
        if (localtime_r(&now_ut, now) == NULL) {
            printf("localtime_r error\n");
            exit(1);
        }

        struct passwd * pw = getpwuid(s.st_uid);
        if ( !pw) {
            printf("getpwuid error\n");
            exit(1);
        }
        struct group * gr = getgrgid(s.st_gid);
        if ( !gr) {
            printf("getgrgid error\n");
            exit(1);
        }

        int buf_size = sprintf(buf, "%c%c%c%c%c%c%c%c%c%c %lu %s %s %5lu %02d %2d ",
            (s.st_mode & S_IFDIR ? 'd' : '-'),
            (s.st_mode & S_IRUSR ? 'r' : '-'),
            (s.st_mode & S_IWUSR ? 'w' : '-'),
            (s.st_mode & S_IXUSR ? 'x' : '-'),
            (s.st_mode & S_IRGRP ? 'r' : '-'),
            (s.st_mode & S_IWGRP ? 'w' : '-'),
            (s.st_mode & S_IXGRP ? 'x' : '-'),
            (s.st_mode & S_IROTH ? 'r' : '-'),
            (s.st_mode & S_IWOTH ? 'w' : '-'),
            (s.st_mode & S_IXOTH ? 'x' : '-'),
            s.st_nlink,
            pw -> pw_name, gr -> gr_name,
            s.st_size,
            mtime -> tm_mon + 1, mtime -> tm_mday);

        if (mtime -> tm_year == now -> tm_year)
            buf_size += sprintf(buf + buf_size, "%02d:%02d ", mtime->tm_hour, mtime->tm_min);
        else
            buf_size += sprintf(buf + buf_size, "%5d ", mtime->tm_year + 1900);


        buf_size += sprintf(buf + buf_size, "<a href=\"");

        char name[256];
        int i;
        for (i = strlen(path) - 1; i >= 0; i--)
            if (path[i] == '/')
                break;

        if (i > 0)
            i++;
        else
            i = 0;

        int j = i;
        while (i < strlen(path))
            buf_size += sprintf(buf + buf_size, "%c", path[i++]);
        if (s.st_mode & S_IFDIR)
            buf_size += sprintf(buf + buf_size, "/\">");
        else
            buf_size += sprintf(buf + buf_size, "\" target=\"_blank\">");

        while (j < strlen(path))
            buf_size += sprintf(buf + buf_size, "%c", path[j++]);
        sprintf(buf + buf_size, "</a></br>");
    }
}


void print_dir(const char path[], int sd) {

    struct dirent **namelist;

    // scans the directory in the alphabetical order
    int n = scandir(path, &namelist, NULL, alphasort);

    if (n < 0) {
        printf("scandir error\n");
        exit(1);
    }

    char ** script = (char **) malloc(sizeof(char *) * n);
    int i;
    for (i = 0; i < n; i++)
        script[i] = (char *) malloc(1024);

    sprintf(script[0], "<h2>%s</h2>", path);
    char fpath[4096];
    strcpy(fpath, path);
    strcat(fpath, "..");

    PrintInfo(fpath, script[1]);

    // next directories and files
    int k = 2;
    for (i = 2; i < n; i++) {
        if (namelist[i] -> d_name[0] != '.') {
            strcpy(fpath, path);
            strcat(fpath, namelist[i] -> d_name);
            PrintInfo(fpath, script[k]);
            k++;
        }
    }

    if (send(sd, OK, strlen(OK), 0) == -1) {
        printf("send error\n");
        exit(1);
    }

    if (send(sd, BEGIN, strlen(BEGIN), 0) == -1) {
        printf("send error\n");
        exit(1);
    }

    for (i = 0; i < k; i++) {
        if (send(sd, script[i], strlen(script[i]), 0) == -1) {
            printf("send error\n");
            exit(1);
        }
        free(script[i]);
    }

    if (send(sd, END, strlen(END), 0) == -1) {
        printf("send error\n");
        exit(1);
    }

    free(script);
    free(namelist);
}

void Download(const char path[], size_t size, int sd) {

    char D[512] = "\
    HTTP/1.1 200 Ok\r\n\
    Connection: close\r\n\
    Accept-Ranges:  none\r\n\
    Content-Type: application/";


    int i;
    for (i = strlen(path) - 1; i >= 0; i--)
        if (path[i] == '.')
            break;

    if (i > 0) {
        strcat(D, path + i + 1);
        strcat(D, "\r\n\r\n");
    }
    else
        strcat(D, "txt\r\n\r\n");

    if (send(sd, D, strlen(D), 0) == -1) {
        printf("send error\n");
        exit(1);
    }

    int f = open(path, 0);

    if (sendfile(sd, f, NULL, size) == -1) {
        printf("sendfile error\n");
        exit(1);
    }

    close(f);
}

char *Conv(char path[]) {
    char *ans = malloc(1024);
    int i, k = 0;
    for (i = 0; i < strlen(path); i++) {
        if (path[i] == '%') {
            sscanf(path + i + 1, "%2X", &ans[k++]);
            i += 2;
        }
        else
            ans[k++] = path[i];
    }
    ans[k] = '\0';
    return ans;
}

void Connection(int sd) {

    char buf[1024];

    //recieving a message from the socket
    int buf_size = recv(sd, buf, 1024, 0);

    //continue working if the message begins with 'GET '
    if (buf[0] != 'G' || buf[1] != 'E' || buf[2] != 'T' || buf[3] != ' ')
        exit(1);


    // example: GET / HTTP/1.1
    int j;
    for (j = 4; j < buf_size; j++)
    	if (buf[j] == ' ') break;

    //checking if 'HTTP/1.1' goes after j
    char protocol[] = "HTTP/1.1";
    int k;
    for (k = 0; k < 8; k++) {
        if (buf[k+j+1] != protocol[k]) {
            exit(1);
        }
    }

    char path[1024];

    strncpy(path, buf+3, j-3);
    //changing ' ' to '.'
    path[0] = '.';
    path[j-3] = '\0';

    //converting the path and rewriting a converted version in path
    char *ans = Conv(path);
    strcpy(path, ans);
    free(ans);
    printf("%s\n", path);

    printf("path: %s\n", path + 1);

    if ( check_path(path) == 0 ) {
        printf("Incorrect input\n");
        exit(3);
    }

    struct stat path_info;
    if (stat(path, &path_info) == -1) {
        printf("stat error\n");
        exit(1);
    }

    if (path_info.st_mode & S_IFDIR)
        print_dir(path, sd); //print the directory
    else
        Download(path, path_info.st_size, sd);
}

int main( ) {
    //creating the socket
    int s = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in locaddr;

    /*
    struct sockaddr_in {
        short int          sin_family;  // Семейство адресов
        unsigned short int sin_port;    // Номер порта
        struct in_addr     sin_addr;    // IP-адрес
        unsigned char      sin_zero[8]; // "Дополнение" до размера структуры sockaddr
    };
    */
    locaddr.sin_family = AF_INET;
    locaddr.sin_port = htons(8080);
    locaddr.sin_addr.s_addr = inet_addr("0.0.0.0");

    //Allowing the socket to be bound to an address that is already in use
    //The socket option SO_REUSEADDR for which the value optval is to be set

    int optval = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        printf("Setsockopt error\n");
        exit(1);
    }

    //connecting the socket with the locaddr
    if (bind(s, (const struct sockaddr *) &locaddr, sizeof(struct sockaddr_in)) != 0) {
        printf("Bind error\n");
        exit(1);
    }

    //creating a queue of max BACKLOG requests
    if (listen(s, BACKLOG) == -1) {
        printf ("listen error\n");
        exit(1);
    }

    while (1) {

        //creating a new socket for the connection
        int s_new = accept(s, NULL, NULL);
        if (s_new == -1) {
            printf("accept error\n");
            exit(1);
        }

        int fork_result = fork(); // child is working on a query, parent is acceprint new requests in every cycle
        if (fork_result < 0) {
            printf("fork error\n");
            exit(1);
        }

        if ( fork_result == 0 ) {
            //child
            if (close(s) == -1) {
                printf("close error\n");
                exit(1);
            }

            Connection(s_new); //working on a query

            if (close(s_new) == -1) {
                printf("close error\n");
                exit(1);
            }

            exit(EXIT_SUCCESS);
        }

        //parent
        if (close(s_new) == -1){
            printf("close error\n");
            exit(1);
        }
    }

    return 0;
}

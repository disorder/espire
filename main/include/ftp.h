#ifndef __FTP_H__
#define __FTP_H__

#define __unix__
#define FTPLIB_ACCEPT_TIMEOUT 30
#define FTPLIB_BUFSIZ 1024
#include "ftplib.h"
#include "module.h"

typedef struct ftp_request ftp_request_t;
struct ftp_request
{
    char *user;
    char *password;
    char *host;
    int port;
    char *path;
    char *filename;

    task_t *task;
    void (*callback)(ftp_request_t *req, int success);
    void (*callback_stream)(ftp_request_t *req);
    int pad;
    char *buf;
    size_t bufsize;
    // custom data
    void *data;
};

//void ftp_init();
void ftp_get(ftp_request_t *req);


#endif /* __FTP_H__ */

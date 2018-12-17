#ifndef __WEBSERV_MSG_H
#define __WEBSERV_MSG_H


/* constants */
enum {
   MSG_ESUCCESS = 0, // no error
   MSG_EAGAIN,   // interrupt/blocking "errors"
   MSG_ECONN,    // connection errors
   MSG_ESERV     // internal server error
};

#define HM_HDR_SEP    ": "
#define HM_ENT_TERM   "\r\n"
#define HM_VERSION_PREFIX "HTTP/"

#define HM_TEXT_INIT   0x1000
#define HM_NHEADERS_INIT 16
#define HM_HDRSTR_INIT 0x0800

#define HM_HDR_CONTENTTYPE  "Content-Type"
#define HM_HDR_CONTENTLEN   "Content-Length"
#define HM_HDR_LASTMODIFIED "Last-Modified"
#define HM_HDR_DATE         "Date"
#define HM_HDR_SERVER       "Server"
#define HM_HDR_CONNECTION   "Connection"

#define HM_HTTP_VERSION "1.1"

/* types */
typedef enum {
   M_NONE = 0,
   M_GET
} httpreq_method_t;

typedef struct {
   httpreq_method_t method;
   char *uri;
   char *version; // not including leading HTTP/
} httpreq_line_t;

/* HTTP response statuses */
typedef struct {
   int code;
   const char *phrase;
} httpres_stat_t;

/* HTTP response line */
typedef struct {
   char *version; // not including leading HTTP/
   const httpres_stat_t *status;
} httpres_line_t;

/* HTTP message header */
typedef struct {
   char *key;
   char *value;
} httpmsg_header_t;

/* HTTP message */
typedef struct {
   union {
      httpreq_line_t reql;
      httpres_line_t resl;
   } hm_line;
   httpmsg_header_t *hm_headers;
   size_t hm_nheaders;
   httpmsg_header_t *hm_headers_endp;
   char *hm_body;
   char *hm_body_ptr;
   size_t hm_body_size;
   char *hm_text; // full message contents (hdrs + body)
   size_t hm_text_size;
   char *hm_text_ptr;
} httpmsg_t;

/* prototypes */
size_t message_textfree(const httpmsg_t *msg);
void message_init(httpmsg_t *msg);
void message_delete(httpmsg_t *msg);
int message_resize_headers(size_t new_nheaders, httpmsg_t *msg);
int message_resize_body(size_t newsize, httpmsg_t *msg);
int message_resize_text(size_t newsize, httpmsg_t *msg);
int message_error(int msg_errno);

#endif

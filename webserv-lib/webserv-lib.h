

/* HTTP request methods */
typedef enum {
   M_NONE = 0,
   M_GET
} httpreq_method_t;

/* HTTP request line */
typedef struct {
   httpreq_method_t method;
   char *uri;
   char *version; // not including leading HTTP/
} httpreq_line_t;

/* HTTP response codes */

#define C_OK        200
#define C_NOTFOUND  404
#define C_FORBIDDEN 403

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
} httpmsg_t;


int server_start(const char *port, int backlog);
int server_accept(int servfd);
int server_handle_req(int conn_fd, const char *docroot, const char *servname, httpmsg_t *req);
int server_handle_get(int conn_fd, const char *docroot, const char *servname, httpmsg_t *req);

size_t message_bodyfree(const httpmsg_t *msg);
void message_init(httpmsg_t *msg);
int message_resize_headers(size_t new_nheaders, httpmsg_t *req);
int message_resize_body(size_t newsize, httpmsg_t *req);

int request_read(int conn_fd, httpmsg_t *req);
int request_parse(httpmsg_t *req);
void request_delete(httpmsg_t *req);
int request_document_find(const char *docroot, char **pathp, httpmsg_t *req);

void response_delete(httpmsg_t *res);
int response_insert_line(int code, const char *version, httpmsg_t *res);
int response_insert_header(const char *key, const char *val, httpmsg_t *res);
int response_insert_body(const char *body, size_t bodylen, const char *type, httpmsg_t *res);
int response_insert_file(const char *path, httpmsg_t *res);
int response_insert_genhdrs(httpmsg_t *res);
int response_insert_servhdrs(const char *servname, httpmsg_t *res);
httpres_stat_t *response_find_status(int code);
int response_send(int conn_fd, httpmsg_t *res);

const char *hr_meth2str(httpreq_method_t meth);
httpreq_method_t hr_str2meth(const char *str);

enum {
   DOC_FIND_ESUCCESS = 0,
   DOC_FIND_EINTERNAL,
   DOC_FIND_ENOTFOUND
};

#define HM_HDR_SEP    ": "
#define HM_ENT_TERM   "\r\n"
#define HM_VERSION_PREFIX "HTTP/"

#define HM_BODY_INIT   0x1000
#define HM_NHEADERS_INIT 16
#define HM_HDRSTR_INIT 0x0800

#define HM_HDR_CONTENTTYPE  "Content-Type"
#define HM_HDR_CONTENTLEN   "Content-Length"
#define HM_HDR_LASTMODIFIED "Last-Modified"
#define HM_HDR_DATE         "Date"
#define HM_HDR_SERVER       "Server"
#define HM_HDR_CONNECTION   "Connection"

#define C_NOTFOUND_BODY  "Not Found"
#define C_FORBIDDEN_BODY "Forbidden"

#define HM_HTTP_VERSION "1.1"

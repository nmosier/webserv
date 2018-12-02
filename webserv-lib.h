

/* HTTP request methods */
typedef enum {
   HR_M_GET
} httpreq_method_t;

/* HTTP request line */
typedef struct {
   httpreq_method_t method;
   char *uri;
   char *version; // not including leading HTTP/
} httpreq_line_t;

/* HTTP request header */
typedef struct {
   char *key;
   char *value;
} httpreq_header_t;

/* HTTP request */
typedef struct {
   httpreq_line_t hr_line;
   httpreq_header_t *hr_headers; // null-termianted array of headers
   int hr_nheaders;
   char *hr_text; // request as unparsed string
   size_t hr_text_size;
   char *hr_text_endp; // ptr to end of string
} httpreq_t;


int server_start(const char *port, int backlog);
int request_init(httpreq_t *req);
int request_read(int servsock_fd, int conn_fd, httpreq_t *req);
int request_parse(httpreq_t *req);
void request_delete(httpreq_t *req);
int request_resize_headers(size_t new_nheaders, httpreq_t *req);
int request_resize_text(size_t newsize, httpreq_t *req);

const char *hr_meth2str(httpreq_method_t meth);
httpreq_method_t hr_str2meth(const char *str);

enum {
   REQ_RD_RSUCCESS,
   REQ_RD_RERROR,
   REQ_RD_RAGAIN
};
#define REQ_RD_BUFSIZE  (0x10-1)
#define REQ_RD_NHEADERS 1

enum {
   REQ_PRS_RSUCCESS,
   REQ_PRS_RSYNTAX,
   REQ_PRS_RERROR
};



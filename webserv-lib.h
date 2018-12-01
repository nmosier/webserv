

/* HTTP request methods */
typedef enum {
   case HR_M_GET
} httpreq_method_t;

/* HTTP request line */
typedef struct {
   httpreq_method_t method;
   char *uri;
   char *http_version; // not including leading HTTP/
} httpreq_line_t;

/* HTTP request header */
typedef struct {
   char *key;
   char *value;
} httpreq_header_t;

/* HTTP request */
typedef struct {
   httpreq_line_t *hr_line;
   httpreq_header_t *hr_headers; // null-termianted array of headers
   int hr_nheaders;
   char *hr_text; // request as unparsed string
   size_t hr_text_size;
   char *hr_text_endp; // ptr to end of string
} httpreq_t;


int server_start(const char *port, int backlog);
int request_read(int servsock_fd, int conn_fd, httpreq_t **reqp);
void request_delete(httpreq_t *req);

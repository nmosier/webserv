

/* HTTP request methods */
typedef enum {
   M_NONE = 0,
   M_GET
} httpreq_method_t;

/* HTTP request line */
typedef struct {
   httpreq_method_t method;
   off_t uri;
   off_t version; // not including leading HTTP/
} httpreq_line_t;

/* HTTP response codes */

#define C_OK        200
#define C_NOTFOUND  404
#define C_FORBIDDEN 403

/* HTTP response statuses */
typedef struct {
   int code;
   char *phrase;
} httpres_stat_t;

/* HTTP response line */
typedef struct {
   off_t version; // not including leading HTTP/
   httpres_stat_t *status;
} httpres_line_t;

/* HTTP message header */
typedef struct {
   off_t key;
   off_t value;
} httpmsg_header_t;

/* HTTP message */
typedef struct {
   union {
      httpreq_line_t reql;
      httpres_line_t resl;
   } hm_line;
   httpmsg_header_t *hm_headers; // null-termianted array of headers
   int hm_nheaders;
   httpmsg_header_t *hm_headers_endp;
   off_t hm_body;
   char *hm_text; // request as unparsed string
   size_t hm_text_size;
   char *hm_text_endp; // ptr to end of string
} httpmsg_t;


int server_start(const char *port, int backlog);
int server_handle_get(int conn_fd, const char *docroot, httpmsg_t *req);
httpmsg_t *message_init();
int request_read(int servsock_fd, int conn_fd, httpmsg_t *req);
int request_parse(httpmsg_t *req);
void message_delete(httpmsg_t *req);
int message_resize_headers(size_t new_nheaders, httpmsg_t *req);
int message_resize_text(size_t newsize, httpmsg_t *req);

int response_insert_line(int code, const char *version, httpmsg_t *res);
int response_insert_header(const char *key, const char *val, httpmsg_t *res);
int response_insert_body(const char *body, const char *type, httpmsg_t *res);
int response_insert_file(const char *path, httpmsg_t *res);
httpres_stat_t *response_find_status(int code);
int response_send(int conn_fd, httpmsg_t *res);

int document_find(const char *docroot, char *path, size_t pathlen, httpmsg_t *req);

const char *hr_meth2str(httpreq_method_t meth);
httpreq_method_t hr_str2meth(const char *str);

enum {
   DOC_FIND_ESUCCESS = 0,
   DOC_FIND_EINTERNAL,
   DOC_FIND_ENOTFOUND
};


enum {
   REQ_RD_RSUCCESS,
   REQ_RD_RERROR,
   REQ_RD_RAGAIN
};

#define HTTPMSG_TEXTSZ (0x1000-1)
#define HTTPMSG_NHEADS 10

enum {
   REQ_PRS_RSUCCESS,
   REQ_PRS_RSYNTAX,
   REQ_PRS_RERROR
};


#define HM_OFF2STR(off, msg)  (msg->hm_text + off)
#define HM_STR2OFF(str, msg)  (str - msg->hm_text)

#define HM_TEXTFREE(req) ((req)->hm_text_size - ((req)->hm_text_endp - (req)->hm_text))


#define HM_HDR_SEP    ": "
#define HM_HDR_SEPLEN strlen(HM_HDR_SEP)
#define HM_ENT_TERM   "\r\n"
#define HM_ENT_TERMLEN strlen(HM_ENT_TERM)
#define HM_VERSION_PREFIX "HTTP/"


#define HM_HDR_CONTENTTYPE "Content-Type"


#define C_NOTFOUND_BODY "Not Found"
#define C_FORBIDDEN_BODY "Forbidden"

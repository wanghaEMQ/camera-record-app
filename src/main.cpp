#include <stdio.h>
#include <unistd.h>
#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include <nng/supplemental/util/platform.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pipeline0/push.h>

#include "pthread.h"

#define START_RECORD "start-record"
#define STOP_RECORD  "stop-record"
#define PREVIEW      "preview"

#define NEXT_STOP 0
#define NEXT_START 1

const char *preview_dir = "/home/wangha/Documents/git/camera-goweb-app/images/";
const char *preview_prefix = "preview";
const char *preview_ext = "jpg";
int         preview_idx = 0;
const int   preview_cap = 20;
int         preview_req_cnt = 0;

int         g_camera_running = 0;
int         g_camera_next = NEXT_STOP; // 0->stop 1->start

using namespace cv;
using namespace std;

nng_mtx    *http_mtx;
Mat         g_http_preview;

std::vector<uchar> preview_buf;

typedef struct rest_job {
	nng_aio *        http_aio; // aio from HTTP we must reply to
	nng_http_res *   http_res; // HTTP response object
	struct rest_job *next;     // next on the freelist
} rest_job;

// We maintain a queue of free jobs.  This way we don't have to
// deallocate them from the callback; we just reuse them.
nng_mtx * job_lock;
rest_job *job_freelist;

void
fatal(const char *m, int rv)
{
	fprintf(stderr, "%s: %s\n", m, nng_strerror(rv));
	//exit(1);
}

#include <sys/stat.h>
#include <sys/types.h>
int
readfile(const char *fname, char *data)
{
	FILE *      f;
	size_t      len;
	struct stat st;
	int         rv;

	if ((f = fopen(fname, "rb")) == NULL) {
		return -1;
	}
	if (stat(fname, &st) != 0) {
		(void) fclose(f);
		return -1;
	}
	len = st.st_size;
	if (len > 0) {
		if (fread(data, 1, len, f) != len) {
			rv = -1;
			goto done;
		}
		rv = len;
	} else {
		rv = -2;
	}
done:
	(void) fclose(f);
	return (rv);

}

static void
rest_recycle_job(rest_job *job)
{
	if (job->http_res != NULL) {
		nng_http_res_free(job->http_res);
		job->http_res = NULL;
	}

	nng_mtx_lock(job_lock);
	job->next    = job_freelist;
	job_freelist = job;
	nng_mtx_unlock(job_lock);
}

static void
rest_http_fatal(rest_job *job, const char *fmt, int rv)
{
	char          buf[128];
	nng_aio *     aio = job->http_aio;
	nng_http_res *res = job->http_res;

	job->http_res = NULL;
	job->http_aio = NULL;
	snprintf(buf, sizeof(buf), fmt, nng_strerror(rv));
	nng_http_res_set_status(res, NNG_HTTP_STATUS_INTERNAL_SERVER_ERROR);
	nng_http_res_set_reason(res, buf);
	nng_aio_set_output(aio, 0, res);
	nng_aio_finish(aio, 0);
	rest_recycle_job(job);
}

static rest_job *
rest_get_job(void)
{
	rest_job *job;

	nng_mtx_lock(job_lock);
	if ((job = job_freelist) != NULL) {
		job_freelist = job->next;
		nng_mtx_unlock(job_lock);
		job->next = NULL;
		return (job);
	}
	nng_mtx_unlock(job_lock);
	if ((job = (rest_job *)calloc(1, sizeof(*job))) == NULL) {
		return (NULL);
	}
	return (job);
}

void
http_handle(nng_aio *aio)
{
	struct rest_job *job;
	nng_http_req *   req  = (nng_http_req *)nng_aio_get_input(aio, 0);
	nng_http_conn *  conn = (nng_http_conn *)nng_aio_get_input(aio, 2);
	size_t           sz;
	int              rv;
	void *           data;

	if ((job = rest_get_job()) == NULL) {
		nng_aio_finish(aio, NNG_ENOMEM);
		return;
	}
	if ((rv = nng_http_res_alloc(&job->http_res)) != 0) {
		rest_recycle_job(job);
		nng_aio_finish(aio, rv);
		fatal("nng_http_res_alloc", rv);
		return;
	}

	nng_http_req_get_data(req, &data, &sz);
	job->http_aio = aio;
	printf("R> (%ld)%.*s\n", sz, (int)sz, (char*)data);

	if (0 == strcmp((char *)data, START_RECORD)) {
		g_camera_next = NEXT_START;
		rv = nng_http_res_copy_data(job->http_res, START_RECORD, strlen(START_RECORD));
		printf("[record] start %d\n", rv);
	} else if (0 == strcmp((char *)data, STOP_RECORD)) {
		g_camera_next = NEXT_STOP;
		rv = nng_http_res_copy_data(job->http_res, STOP_RECORD, strlen(STOP_RECORD));
		printf("[record] stop %d\n", rv);
	}

	size_t preview_sz;
	char   pszstr[100];
	// Reply to the client
	nng_mtx_lock(http_mtx);
	if (g_camera_running == 1
	    && g_http_preview.total() > 0
	    && 0 == strcmp((char *)data, PREVIEW)) {
		bool imrv = imencode(".jpg", g_http_preview, preview_buf);
		rv = nng_http_res_copy_data(job->http_res, (char *)preview_buf.data(), preview_buf.size());
		sprintf(pszstr, "%ld", preview_buf.size());
		nng_http_res_set_header(job->http_res, "Content-type", "image/jpeg");
		nng_http_res_set_header(job->http_res, "Content-length", pszstr);
	}
	nng_mtx_unlock(http_mtx);

	if (rv != 0) {
		fatal("nng_http_res_copy_data", rv);
		rest_recycle_job(job);
		nng_aio_finish(aio, rv);
		return;
	}
	// Set the output - the HTTP server will send it back to the
	// user agent with a 200 response.
	nng_aio_set_output(job->http_aio, 0, job->http_res);
	nng_aio_finish(job->http_aio, 0);
	printf("S> (%ld)%.*s\n", sz, (int)sz, (char*)data);
	return;
}

int
start_http(const char *urlstr)
{
	printf("[http] start\n");
	nng_http_server *server;
	int              rv;
	nng_url *        url;
	nng_http_handler *handler;

	if ((rv = nng_url_parse(&url, urlstr)) != 0) {
		fatal("nng_url_parse", rv);
	}
	if ((rv = nng_mtx_alloc(&job_lock)) != 0) {
		fatal("nng_mtx_alloc", rv);
	}
	// Get a suitable HTTP server instance.  This creates one
	// if it doesn't already exist.
	if ((rv = nng_http_server_hold(&server, url)) != 0) {
		fatal("nng_http_server_hold", rv);
	}

	// Allocate the handler - we use a dynamic handler for REST
	// using the function "rest_handle" declared above.
	rv = nng_http_handler_alloc(&handler, url->u_path, http_handle);
	if (rv != 0) {
		fatal("nng_http_handler_alloc", rv);
	}
	if ((rv = nng_http_handler_set_method(handler, "POST")) != 0) {
		fatal("nng_http_handler_set_method", rv);
	}
	// We want to collect the body, and we (arbitrarily) limit this to
	// 128KB.  The default limit is 1MB.  You can explicitly collect
	// the data yourself with another HTTP read transaction by disabling
	// this, but that's a lot of work, especially if you want to handle
	// chunked transfers.
	if ((rv = nng_http_handler_collect_body(handler, true, 1024 * 1024)) !=
	    0) {
		fatal("nng_http_handler_collect_body", rv);
	}
	if ((rv = nng_http_server_add_handler(server, handler)) != 0) {
		fatal("nng_http_handler_add_handler", rv);
	}
	if ((rv = nng_http_server_start(server)) != 0) {
		fatal("nng_http_server_start", rv);
	}

	nng_url_free(url);
	printf("[http] end\n");
	return 0;
}

void
update_preview_name(char *preview_fname)
{
	sprintf(preview_fname, "%s%s%d.%s", preview_dir,
			preview_prefix, preview_idx, preview_ext);
	printf("[preview] write to %s\n", preview_fname);
	preview_idx = (preview_idx+1) % preview_cap;
}

void*
cv_cb(void *arg){
	(void*)arg;
	char preview_fname[128];
	Mat src;
	// use default camera as video source
	VideoCapture cap(0);
	cap.set(CAP_PROP_FRAME_WIDTH, 640);
	cap.set(CAP_PROP_FRAME_HEIGHT, 480);
	// check if we succeeded
	if (!cap.isOpened()) {
	    cerr << "ERROR! Unable to open camera\n";
	    return NULL;
	}
	// get one frame from camera to know frame size and type
	cap >> src;
	// check if we succeeded
	if (src.empty()) {
	    cerr << "ERROR! blank frame grabbed\n";
	    return NULL;
	}
	bool isColor = (src.type() == CV_8UC3);
	
	//--- INITIALIZE VIDEOWRITER
	VideoWriter writer;
	int codec = VideoWriter::fourcc('M', 'J', 'P', 'G');  // select desired codec (must be available at runtime)
	double fps = 25.0;                          // framerate of the created video stream
	string filename = "./live.avi";             // name of the output video file
	writer.open(filename, codec, fps, src.size(), isColor);
	//Size S = Size((int)640, (int)480);
	//writer.open(filename, codec, fps, S, isColor);
	// check if we succeeded
	if (!writer.isOpened()) {
	    cerr << "Could not open the output video file for write\n";
	    return NULL;
	}
	
	//--- GRAB AND WRITE LOOP
	cout << "Writing videofile: " << filename << endl;
	while (g_camera_running)
	{
		// check if we succeeded
		if (!cap.read(src)) {
		    cerr << "ERROR! blank frame grabbed\n";
		    break;
		}
		// encode the frame into the videofile stream
		writer.write(src);
		/*
		if (preview_req_cnt > 0) {
			update_preview_name(preview_fname);
			imwrite(preview_fname, src);
			preview_req_cnt --;
		}
		// imshow("Live", src);
		*/
		nng_mtx_lock(http_mtx);
		g_http_preview = src;
		nng_mtx_unlock(http_mtx);
	}
	// the videofile will be closed and released automatically in VideoWriter destructor
	return NULL;
}

void
start_cv(pthread_t *cv_thr)
{
	int rv;
	rv = pthread_create(cv_thr, NULL, cv_cb, NULL);
	if (rv != 0) {
		printf("error create thread %d\n", rv);
		return;
	}
	printf("[cv] start\n");
}
 
void
stop_cv(pthread_t *cv_thr)
{
	int rv;
	int returns;
	rv = pthread_join(*cv_thr, NULL);
	if (rv != 0) {
		printf("error join thread %d\n", rv);
		return;
	}
	printf("[cv] stop\n");
}

int main(int, char**argv)
{
	int       rv;
	pthread_t ipc_thr;
	pthread_t cv_thr;
	nng_mtx_alloc(&http_mtx);
	start_http("http://0.0.0.0:9999");
	preview_buf.resize(1024 * 1024);

	while (1) {
		sleep(1); // TODO
		if (g_camera_next == NEXT_START && g_camera_running == 1) {
			continue;
		} else if (g_camera_next == NEXT_STOP && g_camera_running == 0) {
			continue;
		} else if (g_camera_next == NEXT_START && g_camera_running == 0) {
			// enable
			g_camera_running = 1;
			start_cv(&cv_thr);
		} else if (g_camera_next == NEXT_STOP && g_camera_running == 1) {
			// disable
			g_camera_running = 0;
			stop_cv(&cv_thr);
		}
	}
}

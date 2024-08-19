#include <stdio.h>
#include <unistd.h>
#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>

#include <nng/nng.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pipeline0/push.h>

#include "pthread.h"

#define START_RECORD "start-record"
#define STOP_RECORD "stop-record"
#define PREVIEW "preview"

#define NEXT_STOP 0
#define NEXT_START 1

nng_socket *ipcsock = NULL;
const char *ipc_url = "ipc:///tmp/camerarecord.ipc";
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

void*
ipc_cb(void *arg){
	(void*)arg;
	while (1) {
		nng_msg *msg;
		nng_recvmsg(*ipcsock, &msg, 0);
		if (msg == NULL) {
			continue;
		}
		char *pld = (char *)nng_msg_body(msg);
		if (strlen(pld) == strlen(START_RECORD) &&
			strcmp(pld, START_RECORD) == 0) {
		printf("[record] start\n");
			g_camera_next = NEXT_START;
			goto next;
		}
		else if (strlen(pld) == strlen(STOP_RECORD) &&
			strcmp(pld, STOP_RECORD) == 0) {
		printf("[record] stop\n");
			g_camera_next = NEXT_STOP;
			goto next;
		}
		else if (strlen(pld) == strlen(PREVIEW) &&
			strcmp(pld, PREVIEW) == 0) {
		printf("[preview] once\n");
			if (preview_req_cnt < preview_cap)
				preview_req_cnt ++;
			goto next;
		}
		else {
			printf("unknown cmd\n");
			goto next;
		}
next:
		nng_msg_free(msg);
	}
	return NULL;
}

void*
cv_cb(void *arg){
	(void*)arg;
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
		// show live and wait for a key with timeout long enough to show images
		/*
		imshow("Live", src);
		*/
	}
	// the videofile will be closed and released automatically in VideoWriter destructor
	return NULL;
}

void
start_nng(nng_socket *sock, pthread_t *thr) {
	printf("[nng] start\n");
	int rv;
	rv = nng_pull0_open(sock);
	if (rv != 0) {
		printf("[nng] error open pull0 %d %s\n", rv, nng_strerror(rv));
		return;
	}
	rv = nng_listen(*sock, ipc_url, NULL, 0);
	if (rv != 0) {
		printf("[nng] error listen %d %s\n", rv, nng_strerror(rv));
		return;
	}
	ipcsock = sock;
	rv = pthread_create(thr, NULL, ipc_cb, NULL);
	if (rv != 0) {
		printf("[nng] error create thread %d\n", rv);
		return;
	}

	printf("[nng] end\n");
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

int main(int, char**)
{
	int       rv;
	pthread_t ipc_thr;
	pthread_t cv_thr;
	nng_socket *sock = (nng_socket *)nng_alloc(sizeof(nng_socket));
	start_nng(sock, &ipc_thr);

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

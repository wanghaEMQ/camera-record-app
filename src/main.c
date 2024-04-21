#include <stdio.h>
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/videoio/videoio_c.h>
#include <opencv2/imgcodecs/imgcodecs_c.h>

#include <nng/nng.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pipeline0/push.h>

#include "libuvc/libuvc.h"

#include "pthread.h"

CvVideoWriter *wr = NULL;
int preview_req_cnt = 0;

#define START_RECORD "start-record"
#define STOP_RECORD "stop-record"
#define PREVIEW "preview"

#define NEXT_STOP 0
#define NEXT_START 1

nng_socket *ipcsock = NULL;
const char *ipc_url = "ipc:///tmp/camerarecord.ipc";
const char *preview_path = "/home/wangha/Documents/git/camera-goweb-app/images/preview.jpg";

pthread_t *preview_thr;
int g_camera_running = 0;
int g_camera_next = NEXT_STOP; // 0->stop 1->start

void
writepreview(uvc_frame_t *frame) {
  FILE *fp;

  uvc_frame_t *bgr = NULL;
  bgr = uvc_allocate_frame(frame->width * frame->height * 3);
  if (!bgr) {
    printf("unable to allocate bgr frame!\n");
    return;
  }

  switch (frame->frame_format) {
  case UVC_FRAME_FORMAT_H264:
    printf("IOS Format photo, not supported\n");
    /* use `ffplay H264_FILE` to play */
    /* fp = fopen(H264_FILE, "a");
     * fwrite(frame->data, 1, frame->data_bytes, fp);
     * fclose(fp); */
    break;
  case UVC_COLOR_FORMAT_MJPEG:
    fp = fopen(preview_path, "w");
    fwrite(frame->data, 1, frame->data_bytes, fp);
    fclose(fp);
    break;
  case UVC_COLOR_FORMAT_YUYV:
    /* Do the BGR conversion */
    printf("YUYV Format photo, not supported\n");
	//uvc_any2bgr(frame, bgr);
    fp = fopen(preview_path, "w");
    //fwrite(bgr->data, 1, bgr->data_bytes, fp);
    fwrite(frame->data, 1, frame->data_bytes, fp);
    fclose(fp);
	break;
	/*
    ret = uvc_any2bgr(frame, bgr);
    if (ret) {
      uvc_perror(ret, "uvc_any2bgr");
      uvc_free_frame(bgr);
      return;
    }
    break;
	*/
  default:
    printf("Unknown Format photo, not supported\n");
    break;
  }
  if (bgr)
    uvc_free_frame(bgr);
}

void cb(uvc_frame_t *frame, void *ptr) {
  uvc_frame_t *bgr;
  uvc_error_t ret;
  IplImage* cvImg;
  IplImage* cvImg2;

  bgr = uvc_allocate_frame(frame->width * frame->height * 3);
  if (!bgr) {
    printf("unable to allocate bgr frame!");
    return;
  }

  // save preview
  if (preview_req_cnt > 0) {
    writepreview(frame);
    preview_req_cnt --;
  }

  //ret = uvc_any2bgr(frame, bgr);
  // ret = uvc_any2rgb(frame, bgr);
  //ret = uvc_mjpeg2rgb(frame, bgr);
  ret = uvc_mjpeg2bgr(frame, bgr);
  if (ret) {
    uvc_perror(ret, "uvc_any2bgr");
    uvc_free_frame(bgr);
    return;
  }

  cvImg = cvCreateImageHeader(cvSize(bgr->width, bgr->height), IPL_DEPTH_8U, 3);
  cvSetData(cvImg, bgr->data, bgr->width * 3);

  // Write to disk
  if (wr == NULL) {
    //wr = cvCreateVideoWriter("/tmp/test1.avi", CV_FOURCC('x','v','i','d'),
    wr = cvCreateVideoWriter("/tmp/test1.avi", CV_FOURCC('M','J','P','G'),
      25, cvSize(bgr->width, bgr->height), true);
    //wr = cvCreateVideoWriter("/tmp/test1.avi", CV_FOURCC('M','J','P','G'),
    //wr = cvCreateVideoWriter("test1.avi", CV_FOURCC('x','v','i','d'),
	if (!wr)
		printf("Error in create cv video writer.\n");
  }
  cvWriteFrame(wr, cvImg);

  /* debug
  cvNamedWindow("Test", CV_WINDOW_AUTOSIZE);
  cvShowImage("Test", cvImg);
  cvWaitKey(10);
  */

  // free
  cvReleaseImageHeader(&cvImg);
  uvc_free_frame(bgr);

  /*
  cvImg2 = cvCreateImageHeader(cvSize(bgr->width, bgr->height), IPL_DEPTH_8U, 3);
  void *data2 = nng_alloc(sizeof(char) * bgr->height * bgr->width * 3);
  cvSetData(cvImg2, data2, bgr->width * 3);

  cvConvertImage(cvImg, cvImg2, CV_CVTIMG_SWAP_RB); // rgb -> bgr

  if (wr == NULL) {
    //wr = cvCreateVideoWriter("test.mp4", CV_FOURCC_DEFAULT,
    //wr = cvCreateVideoWriter("/tmp/test1.avi", CV_FOURCC('x','v','i','d'),
    wr = cvCreateVideoWriter("/tmp/test1.avi", CV_FOURCC('M','J','P','G'),
    //wr = cvCreateVideoWriter("/tmp/test1.avi", CV_FOURCC('M','J','P','G'),
    //wr = cvCreateVideoWriter("test1.avi", CV_FOURCC('x','v','i','d'),
      25, cvSize(bgr->width, bgr->height), true);
    //  25, cvSize(frame->width, frame->height), true);
	if (!wr)
		printf("Error in create cv video writer.\n");
  }
  cvWriteFrame(wr, cvImg2);
  */

  //cvReleaseImageHeader(&cvImg2);
  //nng_free(data2, 0);
}

void*
ipc_cb(void *arg){
	(void*)arg;
	while (1) {
		nng_msg *msg;
		nng_recvmsg(*ipcsock, &msg, 0);
		printf("get a msg\n");
		if (msg == NULL) {
			continue;
		}
		char *pld = nng_msg_body(msg);
		if (strlen(pld) == strlen(START_RECORD) &&
			strcmp(pld, START_RECORD) == 0) {
		printf("start record\n");
			g_camera_next = NEXT_START;
			goto next;
		}
		else if (strlen(pld) == strlen(STOP_RECORD) &&
			strcmp(pld, STOP_RECORD) == 0) {
		printf("stop record\n");
			g_camera_next = NEXT_STOP;
			goto next;
		}
		else if (strlen(pld) == strlen(PREVIEW) &&
			strcmp(pld, PREVIEW) == 0) {
		printf("preview \n");
			if (preview_req_cnt < 3)
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

void
start_nng(nng_socket *sock, pthread_t *thr) {
	printf("start nng\n");
	int rv;
	nng_pull0_open(sock);
	rv = nng_listen(*sock, ipc_url, NULL, 0);
	if (rv != 0) {
		printf("error listen %d %s\n", rv, nng_strerror(rv));
		return;
	}
	pthread_create(thr, NULL, ipc_cb, NULL);

	printf("end nng\n");
}

int main(int argc, char **argv) {
  uvc_context_t *ctx;
  uvc_error_t res;
  uvc_device_t *dev;
  uvc_device_handle_t *devh;
  uvc_stream_ctrl_t ctrl;

  pthread_t preview_thr;
  nng_socket *sock = nng_alloc(sizeof(nng_socket));
  start_nng(sock, &preview_thr);
  ipcsock = sock;

  res = uvc_init(&ctx, NULL);

  if (res < 0) {
    uvc_perror(res, "uvc_init");
    return res;
  }

  puts("UVC initialized");

  res = uvc_find_device(
      ctx, &dev,
      0, 0, NULL);

  if (res < 0) {
    uvc_perror(res, "uvc_find_device");
  } else {
    puts("Device found");

    res = uvc_open(dev, &devh);

    if (res < 0) {
      uvc_perror(res, "uvc_open");
    } else {
      puts("Device opened");

      uvc_print_diag(devh, stderr);

      res = uvc_get_stream_ctrl_format_size(
          //devh, &ctrl, UVC_FRAME_FORMAT_YUYV, 640, 480, 30
          devh, &ctrl, UVC_FRAME_FORMAT_MJPEG, 640, 480, 30
      );

      uvc_print_stream_ctrl(&ctrl, stderr);

      if (res < 0) {
        uvc_perror(res, "get_mode");
      } else {
        while (1) {
          sleep(1); //TODO
          if (g_camera_next == NEXT_START && g_camera_running == 1) {
    		printf("-");
            continue;
          } else if (g_camera_next == NEXT_STOP && g_camera_running == 0) {
            continue;
          } else if (g_camera_next == NEXT_START && g_camera_running == 0) {
            // enable
			g_camera_running = 1;
            res = uvc_start_streaming(devh, &ctrl, cb, 12345, 0);
            if (res < 0) {
              uvc_perror(res, "start_streaming");
            } else {
              puts("Start Streaming");
              //uvc_error_t resAEMODE = uvc_set_ae_mode(devh, 1);
              uvc_error_t resAEMODE = uvc_set_ae_mode(devh, 8);
              uvc_perror(resAEMODE, "set_ae_mode");
          /* 
          int i;
          for (i = 1; i <= 10; i++) {
            uvc_error_t resPT = uvc_set_pantilt_abs(devh, i * 20 * 3600, 0);
            uvc_perror(resPT, "set_pt_abs");
            uvc_error_t resEXP = uvc_set_exposure_abs(devh, 20 + i * 50);
            uvc_perror(resEXP, "set_exp_abs");

            sleep(1);
          }
		  */
          //sleep(120);
			}
          } else if (g_camera_next == NEXT_STOP && g_camera_running == 1) {
			g_camera_running = 0;
            uvc_stop_streaming(devh);
			if (wr) {
			  cvReleaseVideoWriter(&wr);
			  wr = NULL;
			}
            puts("Done streaming.");
          }
        }
      }

      uvc_close(devh);
      puts("Device closed");
    }

    uvc_unref_device(dev);
  }

  uvc_exit(ctx);
  puts("UVC exited");

  if (ipcsock) {
	  nng_close(*ipcsock);
	  nng_free(ipcsock, 0);
  }

  return 0;
}


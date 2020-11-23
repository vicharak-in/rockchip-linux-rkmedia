// Copyright 2020 Fuzhou Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <rga/im2d.h>
#include <rga/rga.h>

#include "common/sample_common.h"
#include "rkmedia_api.h"
#include "rkmedia_venc.h"

static RK_U32 g_snap_limit = 1;
static RK_U32 g_raw_img_limit = 0;
static RK_U32 g_unused_num = 0;
static RK_CHAR *g_pOutPath = "/tmp/";
static bool quit = false;
static void sigterm_handler(int sig) {
  fprintf(stderr, "signal %d\n", sig);
  quit = true;
}

typedef struct rga_arg_s {
  RK_U32 u32SrcWidth;
  RK_U32 u32SrcHeight;
  RK_U32 u32RgaX;
  RK_U32 u32RgaY;
  RK_U32 u32RgaWidth;
  RK_U32 u32RgaHeight;
  RK_U32 u32Mode;
} rga_arg_t;

void video_packet_cb(MEDIA_BUFFER mb) {
  static RK_U32 jpeg_id = 0;
  // printf("Get JPEG packet[%d]:ptr:%p, fd:%d, size:%zu, mode:%d, channel:%d, "
  //        "timestamp:%lld\n",
  //        jpeg_id, RK_MPI_MB_GetPtr(mb), RK_MPI_MB_GetFD(mb),
  //        RK_MPI_MB_GetSize(mb), RK_MPI_MB_GetModeID(mb),
  //        RK_MPI_MB_GetChannelID(mb), RK_MPI_MB_GetTimestamp(mb));
  if (jpeg_id < g_snap_limit + g_raw_img_limit) {
    char jpeg_path[64];
    if (jpeg_id < g_raw_img_limit)
      sprintf(jpeg_path, "%s/rga_osd_raw_%d.jpeg", g_pOutPath, jpeg_id);
    else
      sprintf(jpeg_path, "%s/rga_osd_prod_%d.jpeg", g_pOutPath, jpeg_id);
    printf(">>>>>>>>>>>>save rst to %s\n", jpeg_path);
    FILE *file = fopen(jpeg_path, "w");
    if (file) {
      fwrite(RK_MPI_MB_GetPtr(mb), 1, RK_MPI_MB_GetSize(mb), file);
      fclose(file);
    }
  }
  RK_MPI_MB_ReleaseBuffer(mb);
  jpeg_id++;
  if (jpeg_id >= g_snap_limit + g_raw_img_limit)
    quit = true;
}

static void set_argb8888_buffer(RK_U32 *buf, RK_U32 size, RK_U32 color) {
  for (RK_U32 i = 0; buf && (i < size); i++)
    *(buf + i) = color;
}

static IM_STATUS RGA_Rect_draw(rga_buffer_t buf, RK_U32 x, RK_U32 y,
                               RK_U32 width, RK_U32 height, RK_U32 line_pixel) {
  im_rect rect_up = {x, y, width, line_pixel};
  im_rect rect_buttom = {x, y + height - line_pixel, width, line_pixel};
  im_rect rect_left = {x, y, line_pixel, height};
  im_rect rect_right = {x + width - line_pixel, y, line_pixel, height};
  im_rect rect_all = {x, y, width, height};
  IM_STATUS STATUS = imfill(buf, rect_all, 0x00000000);
  STATUS |= imfill(buf, rect_up, 0x0000ff00);
  STATUS |= imfill(buf, rect_buttom, 0x0000ff00);
  STATUS |= imfill(buf, rect_left, 0x0000ff00);
  STATUS |= imfill(buf, rect_right, 0x0000ff00);
  return STATUS;
}

static IM_STATUS RGA_Rect_draw2(rga_buffer_t buf, RK_U32 x, RK_U32 y,
                                RK_U32 width, RK_U32 height,
                                RK_U32 line_pixel) {
  im_rect rect_up = {x, y, width, line_pixel};
  im_rect rect_buttom = {x, y + height - line_pixel, width, line_pixel};
  im_rect rect_left = {x, y, line_pixel, height};
  im_rect rect_right = {x + width - line_pixel, y, line_pixel, height};
  IM_STATUS STATUS = imfill(buf, rect_up, 0x0000ff00);
  STATUS |= imfill(buf, rect_buttom, 0x0000ff00);
  STATUS |= imfill(buf, rect_left, 0x0000ff00);
  STATUS |= imfill(buf, rect_right, 0x0000ff00);
  return STATUS;
}

static IM_STATUS RGA_Rect_Fill(rga_buffer_t buf, RK_U32 x, RK_U32 y,
                               RK_U32 width, RK_U32 height) {
  im_rect rect_up = {x, y, width, height / 2};
  im_rect rect_all = {x, y, width, height};
  IM_STATUS STATUS = imfill(buf, rect_all, 0x00000000);
  STATUS |= imfill(buf, rect_up, 0xff0000ff);
  return STATUS;
}

static void *GetMediaBuffer(void *arg) {
  printf("start %s thread, arg: %p\n", __func__, arg);
  rga_arg_t *rga_arg = (rga_arg_t *)arg;
  MEDIA_BUFFER mb;
  RK_U32 get_cnt = 0;
  while (!quit) {
    mb = RK_MPI_SYS_GetMediaBuffer(RK_ID_VI, 0, -1);
    if (!mb) {
      printf(">>>>>>>>>>>>>RK_MPI_SYS_GetMediaBuffer get null buffer!\n");
      continue;
    }
    if (get_cnt > g_unused_num + g_raw_img_limit &&
        get_cnt <= g_unused_num + g_raw_img_limit + g_snap_limit) {
      if (rga_arg->u32Mode < 3) {
        rga_buffer_t pat;
        rga_buffer_t src;
        MEDIA_BUFFER pat_mb = NULL;
        IM_STATUS STATUS;
        MB_IMAGE_INFO_S stImageInfo = {
            rga_arg->u32RgaWidth, rga_arg->u32RgaHeight, rga_arg->u32RgaWidth,
            rga_arg->u32RgaHeight, IMAGE_TYPE_ARGB8888};
        pat_mb = RK_MPI_MB_CreateImageBuffer(&stImageInfo, RK_TRUE, 0);
        if (!pat_mb) {
          printf("ERROR: RK_MPI_MB_CreateImageBuffer get null buffer!\n");
          break;
        }
        pat = wrapbuffer_virtualaddr(
            RK_MPI_MB_GetPtr(pat_mb), rga_arg->u32RgaWidth,
            rga_arg->u32RgaHeight, RK_FORMAT_RGBA_8888);
        if (rga_arg->u32Mode == 0) {
          STATUS = RGA_Rect_Fill(pat, 0, 0, rga_arg->u32RgaWidth,
                                 rga_arg->u32RgaHeight);
        } else if (rga_arg->u32Mode == 1) {
          STATUS = RGA_Rect_draw(pat, 0, 0, rga_arg->u32RgaWidth,
                                 rga_arg->u32RgaHeight, 4);
        } else if (rga_arg->u32Mode ==
                   2) { // different way to init rect, only for reference
          set_argb8888_buffer(RK_MPI_MB_GetPtr(pat_mb),
                              rga_arg->u32RgaWidth * rga_arg->u32RgaHeight,
                              0x00000000);
          STATUS = RGA_Rect_draw2(pat, 0, 0, rga_arg->u32RgaWidth,
                                  rga_arg->u32RgaHeight, 4);
        }
        src = wrapbuffer_virtualaddr(RK_MPI_MB_GetPtr(mb), rga_arg->u32SrcWidth,
                                     rga_arg->u32SrcHeight,
                                     RK_FORMAT_YCbCr_420_SP);
        im_rect pat_rect = {0, 0, rga_arg->u32RgaWidth, rga_arg->u32RgaHeight};
        im_rect src_rect = {rga_arg->u32RgaX, rga_arg->u32RgaY,
                            rga_arg->u32RgaWidth, rga_arg->u32RgaHeight};
        STATUS = improcess(src, src, pat, src_rect, src_rect, pat_rect,
                           IM_ALPHA_BLEND_DST_OVER);
        if (STATUS != IM_STATUS_SUCCESS) {
          printf(">>>>>>>>>>>>>>>>imblend failed: %s\n", imStrError(STATUS));
          RK_MPI_MB_ReleaseBuffer(pat_mb);
          RK_MPI_MB_ReleaseBuffer(mb);
          quit = true;
          break;
        }
        RK_MPI_SYS_SendMediaBuffer(RK_ID_VENC, 0, mb);
        RK_MPI_MB_ReleaseBuffer(pat_mb);
        RK_MPI_MB_ReleaseBuffer(mb);
      } else {
        // fill rect in nv12, only support white and black
        rga_buffer_t src;
        src = wrapbuffer_virtualaddr(RK_MPI_MB_GetPtr(mb), rga_arg->u32SrcWidth,
                                     rga_arg->u32SrcHeight,
                                     RK_FORMAT_YCbCr_420_SP);
        IM_STATUS STATUS =
            RGA_Rect_draw2(src, rga_arg->u32RgaX, rga_arg->u32RgaY,
                           rga_arg->u32RgaWidth, rga_arg->u32RgaHeight, 4);
        if (STATUS != IM_STATUS_SUCCESS) {
          printf(">>>>>>>>>>>>>>>>RGA_BUF_GET failed: %s\n",
                 imStrError(STATUS));
          RK_MPI_MB_ReleaseBuffer(mb);
          quit = true;
          break;
        }
        RK_MPI_SYS_SendMediaBuffer(RK_ID_VENC, 0, mb);
        RK_MPI_MB_ReleaseBuffer(mb);
      }
    } else if (get_cnt <= g_unused_num + g_raw_img_limit &&
               get_cnt > g_unused_num) {
      RK_MPI_SYS_SendMediaBuffer(RK_ID_VENC, 0, mb);
      RK_MPI_MB_ReleaseBuffer(mb);
    } else {
      RK_MPI_MB_ReleaseBuffer(mb);
    }
    get_cnt++;
  }
  return NULL;
}

static RK_CHAR optstr[] = "?::a::o:r:p:m:";
static const struct option long_options[] = {
    {"aiq", optional_argument, NULL, 'a'},
    {"output", required_argument, NULL, 'o'},
    {"raw_frame", required_argument, NULL, 'r'},
    {"processed_frame", required_argument, NULL, 'p'},
    {"mode", required_argument, NULL, 'm'},
    {"help", optional_argument, NULL, '?'},
    {NULL, 0, NULL, 0},
};

static void print_usage(const RK_CHAR *name) {
  printf("usage example:\n");
#ifdef RKAIQ
  printf("\t%s [-a [iqfiles_dir]] "
         "[-o output_dir] "
         "[-r 0] "
         "[-p 1] "
         "[-m 0] "
         "\n",
         name);
  printf("\t-a | --aiq: enable aiq with dirpath provided, eg:-a "
         "/oem/etc/iqfiles/, "
         "set dirpath empty to using path by default, without this option aiq "
         "should run in other application\n");
#else
  printf("\t%s [-o output_dir] "
         "[-r 0] "
         "[-p 1] "
         "[-m 0] "
         "\n",
         name);
#endif
  printf("\t-o | --output: output dirpath, Default:/tmp/\n");
  printf("\t-r | --raw_frame: number of output raw frames, Default:0\n");
  printf("\t-p | --processed_frame: number of output processed frames, "
         "Default:1\n");
  printf("\t-m | --mode: set 0 to draw filled rect, set 1 to draw rect, "
         "Default:0\n");
}

int main(int argc, char *argv[]) {
  RK_S32 ret;
  rga_arg_t rga_arg;
  memset(&rga_arg, 0, sizeof(rga_arg));
  rga_arg.u32SrcWidth = 1920;
  rga_arg.u32SrcHeight = 1080;
  rga_arg.u32RgaX = 400;
  rga_arg.u32RgaY = 400;
  rga_arg.u32RgaWidth = 320;
  rga_arg.u32RgaHeight = 320;
  IMAGE_TYPE_E enPixFmt = IMAGE_TYPE_NV12;
  const RK_CHAR *pcVideoNode = "rkispp_scale0";
  int c;
  char *iq_file_dir = NULL;
  while ((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1) {
    const char *tmp_optarg = optarg;
    switch (c) {
    case 'a':
      if (!optarg && NULL != argv[optind] && '-' != argv[optind][0]) {
        tmp_optarg = argv[optind++];
      }
      if (tmp_optarg) {
        iq_file_dir = (char *)tmp_optarg;
      } else {
        iq_file_dir = "/oem/etc/iqfiles";
      }
      break;
    case 'o':
      g_pOutPath = optarg;
      break;
    case 'r':
      g_raw_img_limit = atoi(optarg);
      break;
    case 'p':
      g_snap_limit = atoi(optarg);
      break;
    case 'm':
      rga_arg.u32Mode = atoi(optarg);
      break;
    case '?':
    default:
      print_usage(argv[0]);
      return 0;
    }
  }

  printf("#raw frame number: %d\n", g_raw_img_limit);
  printf("#processed frame number: %d\n", g_snap_limit);
  printf("#output dirpath: %s\n", g_pOutPath);
  printf("#mode: %d\n", rga_arg.u32Mode);
  if (iq_file_dir) {
#ifdef RKAIQ
    printf("#Aiq xml dirpath: %s\n\n", iq_file_dir);
    rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
    RK_BOOL fec_enable = RK_FALSE;
    int fps = 30;
    SAMPLE_COMM_ISP_Init(hdr_mode, fec_enable, iq_file_dir);
    SAMPLE_COMM_ISP_Run();
    SAMPLE_COMM_ISP_SetFrameRate(fps);
#endif
  }

  ret = RK_MPI_SYS_Init();
  if (ret) {
    printf("Sys Init failed! ret=%d\n", ret);
    return -1;
  }

  VI_CHN_ATTR_S vi_chn_attr;
  vi_chn_attr.pcVideoNode = pcVideoNode;
  vi_chn_attr.u32BufCnt = 3;
  vi_chn_attr.u32Width = rga_arg.u32SrcWidth;
  vi_chn_attr.u32Height = rga_arg.u32SrcHeight;
  vi_chn_attr.enPixFmt = enPixFmt;
  vi_chn_attr.enWorkMode = VI_WORK_MODE_NORMAL;
  ret = RK_MPI_VI_SetChnAttr(0, 0, &vi_chn_attr);
  ret |= RK_MPI_VI_EnableChn(0, 0);
  if (ret) {
    printf("Create VI[0] failed! ret=%d\n", ret);
    return -1;
  }

  VENC_CHN_ATTR_S venc_chn_attr;
  memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
  venc_chn_attr.stVencAttr.enType = RK_CODEC_TYPE_JPEG;
  venc_chn_attr.stVencAttr.imageType = enPixFmt;
  venc_chn_attr.stVencAttr.u32PicWidth = rga_arg.u32SrcWidth;
  venc_chn_attr.stVencAttr.u32PicHeight = rga_arg.u32SrcHeight;
  venc_chn_attr.stVencAttr.u32VirWidth = rga_arg.u32SrcWidth;
  venc_chn_attr.stVencAttr.u32VirHeight = rga_arg.u32SrcHeight;
  ret = RK_MPI_VENC_CreateJpegLightChn(0, &venc_chn_attr);
  if (ret) {
    printf("Create Venc[0] failed! ret=%d\n", ret);
    return -1;
  }

  MPP_CHN_S stEncChn;
  stEncChn.enModId = RK_ID_VENC;
  stEncChn.s32ChnId = 0;
  ret = RK_MPI_SYS_RegisterOutCb(&stEncChn, video_packet_cb);
  if (ret) {
    printf("Register Output callback failed! ret=%d\n", ret);
    return -1;
  }

  signal(SIGINT, sigterm_handler);
  pthread_t media_thread;
  pthread_create(&media_thread, NULL, GetMediaBuffer, &rga_arg);
  RK_MPI_VI_StartStream(0, 0);

  while (!quit) {
    usleep(500000);
  }

  RK_MPI_VENC_DestroyChn(0);
  RK_MPI_VI_DisableChn(0, 0);

  if (iq_file_dir) {
#ifdef RKAIQ
    SAMPLE_COMM_ISP_Stop();
#endif
  }

  printf("%s exit!\n", __func__);
  return 0;
}

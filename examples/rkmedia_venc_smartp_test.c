// Copyright 2020 Fuzhou Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/sample_common.h"
#include "rkmedia_api.h"
#include "rkmedia_venc.h"

static bool quit = false;
static void sigterm_handler(int sig) {
  fprintf(stderr, "signal %d\n", sig);
  quit = true;
}
static FILE *g_save_file;

#define TEST_ARGB32_YELLOW 0xFFFFFF00
#define TEST_ARGB32_RED 0xFFFF0033

void video_packet_cb(MEDIA_BUFFER mb) {
  const char *nalu_type = "Unknow";
  switch (RK_MPI_MB_GetFlag(mb)) {
  case VENC_NALU_IDRSLICE:
    nalu_type = "IDR Slice";
    break;
  case VENC_NALU_PSLICE:
    nalu_type = "P Slice";
    break;
  default:
    break;
  }

  if (RK_MPI_MB_IsViFrame(mb))
    nalu_type = "VI Slice";

  printf("Get Video Encoded packet[%s]:ptr:%p, fd:%d, size:%zu, mode:%d, "
         "level:%d\n",
         nalu_type, RK_MPI_MB_GetPtr(mb), RK_MPI_MB_GetFD(mb),
         RK_MPI_MB_GetSize(mb), RK_MPI_MB_GetModeID(mb),
         RK_MPI_MB_GetTsvcLevel(mb));
  if (g_save_file)
    fwrite(RK_MPI_MB_GetPtr(mb), 1, RK_MPI_MB_GetSize(mb), g_save_file);
  RK_MPI_MB_ReleaseBuffer(mb);
}

int main(int argc, char *argv[]) {
  int ret = 0;

  RK_MPI_SYS_Init();
#ifdef RKAIQ
  rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
  RK_BOOL fec_enable = RK_FALSE;
  int fps = 30;
  char *iq_file_dir = NULL;
  if ((argc > 1) && !strcmp(argv[1], "-h")) {
    printf("\n\n/Usage:./%s [--aiq iq_file_dir]\n", argv[0]);
    printf("\t --aiq iq_file_dir : init isp\n");
    return -1;
  }
  if (argc == 3) {
    if (strcmp(argv[1], "--aiq") == 0) {
      iq_file_dir = argv[2];
    }
  }
  SAMPLE_COMM_ISP_Init(hdr_mode, fec_enable, iq_file_dir);
  SAMPLE_COMM_ISP_Run();
  SAMPLE_COMM_ISP_SetFrameRate(fps);
#else
  (void)argc;
  (void)argv;
#endif
  VI_CHN_ATTR_S vi_chn_attr;
  vi_chn_attr.pcVideoNode = "rkispp_scale0";
  vi_chn_attr.u32BufCnt = 4;
  vi_chn_attr.u32Width = 1920;
  vi_chn_attr.u32Height = 1080;
  vi_chn_attr.enPixFmt = IMAGE_TYPE_NV12;
  vi_chn_attr.enWorkMode = VI_WORK_MODE_NORMAL;
  ret = RK_MPI_VI_SetChnAttr(0, 1, &vi_chn_attr);
  ret |= RK_MPI_VI_EnableChn(0, 1);
  if (ret) {
    printf("TEST: ERROR: Create vi[0] error! code:%d\n", ret);
    return -1;
  }

  VENC_CHN_ATTR_S venc_chn_attr;
  venc_chn_attr.stVencAttr.enType = RK_CODEC_TYPE_H264;
  venc_chn_attr.stVencAttr.imageType = IMAGE_TYPE_NV12;
  venc_chn_attr.stVencAttr.u32PicWidth = 1920;
  venc_chn_attr.stVencAttr.u32PicHeight = 1080;
  venc_chn_attr.stVencAttr.u32VirWidth = 1920;
  venc_chn_attr.stVencAttr.u32VirHeight = 1080;
  venc_chn_attr.stVencAttr.u32Profile = 77;
  venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
  venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = 30;
  venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = 2000000; // 2Mb
  venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = 1;
  venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = 30;
  venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen = 1;
  venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum = 30;
  ret = RK_MPI_VENC_CreateChn(0, &venc_chn_attr);
  if (ret) {
    printf("TEST: ERROR: Create venc[0] error! code:%d\n", ret);
    return -1;
  }

  printf("### Start smartp mode ....\n");
  VENC_GOP_ATTR_S stGopModeAttr;
  stGopModeAttr.enGopMode = VENC_GOPMODE_SMARTP;
  stGopModeAttr.s32IPQpDelta = 3;
  stGopModeAttr.u32BgInterval = 300;
  stGopModeAttr.u32GopSize = 30;
  ret = RK_MPI_VENC_SetGopMode(0, &stGopModeAttr);
  if (ret) {
    printf("TEST: ERROR: Set gop mode error! code:%d\n", ret);
    return -1;
  }

  g_save_file = fopen("/userdata/output.h264", "w");
  if (!g_save_file)
    printf("#VENC OSD TEST:: Open /userdata/output.h264 failed!\n");

  MPP_CHN_S stEncChn;
  stEncChn.enModId = RK_ID_VENC;
  stEncChn.s32DevId = 0;
  stEncChn.s32ChnId = 0;
  ret = RK_MPI_SYS_RegisterOutCb(&stEncChn, video_packet_cb);
  if (ret) {
    printf("TEST: ERROR: Register cb for venc[0] error! code:%d\n", ret);
    return -1;
  }

  // Bind VI and VENC
  MPP_CHN_S stSrcChn;
  stSrcChn.enModId = RK_ID_VI;
  stSrcChn.s32DevId = 0;
  stSrcChn.s32ChnId = 1;
  MPP_CHN_S stDestChn;
  stDestChn.enModId = RK_ID_VENC;
  stDestChn.s32DevId = 0;
  stDestChn.s32ChnId = 0;
  ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
  if (ret) {
    printf("TEST: ERROR: Bind vi[0] to venc[0] error! code:%d\n", ret);
    return -1;
  }

  printf("%s initial finish\n", __func__);
  signal(SIGINT, sigterm_handler);

  sleep(15);
  printf("### Change to normalp mode ....\n");
  memset(&stGopModeAttr, 0, sizeof(stGopModeAttr));
  stGopModeAttr.enGopMode = VENC_GOPMODE_NORMALP;
  stGopModeAttr.s32IPQpDelta = 3;
  ret = RK_MPI_VENC_SetGopMode(0, &stGopModeAttr);
  if (ret) {
    printf("TEST: ERROR: Set gop mode error! code:%d\n", ret);
    return -1;
  }

  while (!quit) {
    usleep(100);
  }

  if (g_save_file) {
    printf("#VENC OSD TEST:: Close save file!\n");
    fclose(g_save_file);
  }

  printf("%s exit!\n", __func__);
#ifdef RKAIQ
  SAMPLE_COMM_ISP_Stop(); // isp aiq stop before vi streamoff
#endif
  RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
  RK_MPI_VI_DisableChn(0, 1);
  RK_MPI_VENC_DestroyChn(0);

  return 0;
}

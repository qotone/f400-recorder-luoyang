#include <stdio.h>
//#include <vld.h>
#ifndef WIN32
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#endif
#ifdef TEST_BY_FILE
#include "CapturerTest.h"
#else
#include "hisi_cap.h"
#include "fixed_size_log.h"
#endif
#include "encoder.h"
#include "CameraControler.h"
#include "JsonRecordConf.h"
#include "PlayControlList.h"
#include "RenderBuilder.h"
#include "FileDemuxer.h"
#include "hisi_comm.h"
#include "hisi_vid_playback.h"
#include "hisi_aud_playback.h"

#include "config_io.h"
#include "os_msgq.h"
#include "msg_def.h"
#include "minIni.h"
#include "status_def.h"
#include "shm_sem.h"
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/time.h>
#include "osd_fb.h"
#include "MyTime.h"
#include "file_copy.h"
#include "net_control.h"
#include "minIni.h"
#include "hdmi_vi_info.h"


#include "nanomsg/nn.h"
#include "nanomsg/reqrep.h"
#include "../version.h"

#define IPC_URL "ipc:///tmp/reqrep.ipc"
#define MSG_VERSION_REQ "VERSION"
#define MSG_VERSION_RES "recorder.V"recorder_version

void *upgrade_proc(void *arg)
{
    int sock;
    int rv;

    if((sock = nn_socket(AF_SP,NN_REP)) < 0){

        fprintf(stderr, "%s: %s\n","nn_socket",nn_strerror(nn_errno()));
        goto nn_exit;
    }

    if((rv = nn_bind(sock,IPC_URL)) < 0){

//fatal("nn_bind");
        fprintf(stderr, "%s: %s\n","nn_bind",nn_strerror(nn_errno()));
        goto nn_exit;
    }


    for(;;){

        char *buf = NULL;
        int bytes;
        if((bytes = nn_recv(sock, &buf, NN_MSG, 0)) < 0){

//fatal("nn_recv");
        fprintf(stderr, "%s: %s\n","nn_recv",nn_strerror(nn_errno()));
            break;
        }

        if((bytes == (strlen(MSG_VERSION_REQ) + 1) && (strcmp(MSG_VERSION_REQ,buf)) == 0 )){

            if((bytes = nn_send(sock,MSG_VERSION_RES,strlen(MSG_VERSION_RES) + 1, 0))< 0){

//           fatal("nn_send");
        fprintf(stderr, "%s: %s\n","nn_send",nn_strerror(nn_errno()));
                continue;;
            }

        }

        nn_freemsg(buf);
    }

nn_exit:

    pthread_detach(pthread_self());
//	pthread_exit(0);

}

STMediaConfig* g_pMediaCfg = NULL;
int g_inputVolume = 100;
int g_inputMute = 0;


#define _DEMONIZE
extern void net_control_service_proc();
static STMedaiStatus *g_pMediaStatus = NULL;
bool CheckVideoChanOk(int id);
bool CheckAudioChanOk();
int GetFirstVideoInputChanId(const STRecordConfig &recordCfg);
int GetSecondVideoInputChanId(const STRecordConfig &recordCfg);

STMedaiStatus* Cfg_GetConfig()
{
	return g_pMediaStatus;
}


int Cfg_SetRecordConfig(STRecordConfig* pstRecordConfig)
{
	memcpy(&g_pMediaCfg->recordCfg, pstRecordConfig, sizeof(STRecordConfig));

	STMediaConfig *pstConfig = g_pMediaCfg;
	ini_putl("RECORD", "firstVideoInputChanId", pstConfig->recordCfg.firstVideoInputChanId, CONFIG_FILE);
	ini_putl("RECORD", "secondVideoInputChanId", pstConfig->recordCfg.secondVideoInputChanId, CONFIG_FILE);
	ini_putl("RECORD", "enableAudio", pstConfig->recordCfg.enableAudio, CONFIG_FILE);
	ini_putl("RECORD", "enableRecord", pstConfig->recordCfg.enableRecord, CONFIG_FILE);
	return 0;
}


E_PIC_MODE  get_pic_mod(int inputMode)
{
	E_PIC_MODE mode = PIC_MODE_HD720;
	switch(inputMode)
	{
	case 1:
		mode = PIC_MODE_HD720;
		break;
	case 2:
		mode = PIC_MODE_HD1080;
		break;
	case 3:
		mode = PIC_MODE_D1;
		break;
	default:
		break;
	}

	return mode;
}

static bool g_vi2voRenderStart = false;

void StartAllVi2VoRender()
{
	if(g_vi2voRenderStart)return;

	STMediaConfig *pCfg = g_pMediaCfg;

	printf("pCfg->encoderNum=%d\n", pCfg->encoderNum);
	for(int i = 0; i < pCfg->encoderNum; ++i)
	{
		if(pCfg->encoderArr[i].videoFps > 0 && pCfg->encoderArr[i].videoSrcFrameRate > 0)
		{
			int vidMode;
			int srcFrameRate;
			if(get_hdmi_vi_info(pCfg->encoderArr[i].videoInputChan, &vidMode, &srcFrameRate) == 0)
			{//获取实际的vi信息
				pCfg->encoderArr[i].videoMode = vidMode;
				pCfg->encoderArr[i].videoSrcFrameRate = srcFrameRate;
			}
			CRenderBuilder::GetInstance()->RenderHDMItoVo(i + 1, get_pic_mod(pCfg->encoderArr[i].videoMode), pCfg->encoderArr[i].videoFps, pCfg->encoderArr[i].videoSrcFrameRate);
		}
	}

	if(pCfg->recordCfg.enableRecord)
	{
		bool enableAudio =  pCfg->recordCfg.enableAudio;
		if(enableAudio && !CheckAudioChanOk())
		{//
			enableAudio = false;
		}


		int firstVideoChanId = GetFirstVideoInputChanId(pCfg->recordCfg);
		int secondVideoChanId = GetSecondVideoInputChanId(pCfg->recordCfg);

		//更新状态图标
		if(firstVideoChanId > 0)showRecIcon(firstVideoChanId);
		if(secondVideoChanId > 0)showRecIcon(secondVideoChanId);
		if(enableAudio)showAudioRecIcon();
	}
	g_vi2voRenderStart = true;
}


void StartVi2VoRender(int videoNum)
{
	if(g_vi2voRenderStart)return;

	STMediaConfig *pCfg = g_pMediaCfg;

	printf("pCfg->encoderNum=%d\n", pCfg->encoderNum);
	int numOk = 0;
	for(int i = 0; i < pCfg->encoderNum; ++i)
	{
		if(pCfg->encoderArr[i].videoFps > 0 && pCfg->encoderArr[i].videoSrcFrameRate > 0)
		{
			++numOk;
			int vidMode;
			int srcFrameRate;
			if(get_hdmi_vi_info(pCfg->encoderArr[i].videoInputChan, &vidMode, &srcFrameRate) == 0)
			{//获取实际的vi信息
				pCfg->encoderArr[i].videoMode = vidMode;
				pCfg->encoderArr[i].videoSrcFrameRate = srcFrameRate;
			}

			CRenderBuilder::GetInstance()->RenderHDMItoVo(i + 1, get_pic_mod(pCfg->encoderArr[i].videoMode), pCfg->encoderArr[i].videoFps, pCfg->encoderArr[i].videoSrcFrameRate);
			if(videoNum == numOk)break;
		}
	}


	g_vi2voRenderStart = true;
}

void StopAllVi2VoRender()
{
	const STMediaConfig *pCfg = g_pMediaCfg;
	for(int i = 0; i < pCfg->encoderNum; ++i)
	{
		CRenderBuilder::GetInstance()->StopRenderHDMItoVo(i + 1);
	}
	g_vi2voRenderStart = false;


	//隐藏状态图标
	hideAudioRecIcon();
	hideRecIcon();
}

bool CheckVideoChanOk(int id)
{
	const STMediaConfig *pCfg = g_pMediaCfg;
	const STEncoderItem *e;
	e = &pCfg->encoderArr[id - 1];

	if(e->videoInputChan == 0 ||
		e->videoMode == 0 ||
		e->videoFps == 0 ||
		e->videoSrcFrameRate == 0 ||
		e->videoBitrate == 0 ||
		e->videoCodecType == 0)
	{
		return false;
	}

	return true;
}

bool CheckAudioChanOk()
{
	const STMediaConfig *pCfg = g_pMediaCfg;
	if(pCfg->audioCfg.sampleRate > 0 &&  pCfg->audioCfg.audioBitrate > 0)
	{
		return true;
	}

	return false;
}

int GetFirstVideoInputChanId(const STRecordConfig &recordCfg)
{
	if(recordCfg.firstVideoInputChanId > 0)return recordCfg.firstVideoInputChanId;

	//第一个通道未配置，把第二个通道当作第一个通道
	return recordCfg.secondVideoInputChanId;
}

int GetSecondVideoInputChanId(const STRecordConfig &recordCfg)
{
	if(recordCfg.firstVideoInputChanId > 0)
	{//只有当第1个通道已经配置的情况下，才返回第2个通道
		return recordCfg.secondVideoInputChanId;
	}

	return -1;
}

bool StartRecord()
{
	STRecordConfig stRecordCfg;

	const STMediaConfig *pCfg = g_pMediaCfg;
	memcpy(&stRecordCfg, &pCfg->recordCfg, sizeof(STRecordConfig));
	stRecordCfg.enableRecord = 1;

	Cfg_SetRecordConfig(&stRecordCfg);

	bool enableAudio =  pCfg->recordCfg.enableAudio;
	if(enableAudio && !CheckAudioChanOk())
	{//
		enableAudio = false;
		g_pMediaStatus->recordStatus = 2;
		LogDebug("WARN: audio param is invalid, disable audio recording\n");
		strcpy(g_pMediaStatus->recordErrorMsg, "WARN: Audio param is invalid");
	}

	int firstVideoChanId = GetFirstVideoInputChanId(pCfg->recordCfg);
	int secondVideoChanId = GetSecondVideoInputChanId(pCfg->recordCfg);

	LogDebug("firstVideoChanId:%d, secondVideoChanId:%d, sampleRate:%d, audioBitrate:%d, enableAudio:%d,\n",
		firstVideoChanId, secondVideoChanId,
		pCfg->audioCfg.sampleRate, pCfg->audioCfg.audioBitrate, enableAudio);

	set_encoder_audio_settings(firstVideoChanId, enableAudio,
		pCfg->audioCfg.sampleRate, 2, pCfg->audioCfg.audioBitrate);


	if(firstVideoChanId <= 0 || !CheckVideoChanOk(firstVideoChanId))
	{
		LogError("First Video Chan param is not config ok\n");
		g_pMediaStatus->recordStatus = 99;
		strcpy(g_pMediaStatus->recordErrorMsg, "ERROR: First Video param is not config ok");
		return false;
	}


	if(secondVideoChanId > 0 && !CheckVideoChanOk(secondVideoChanId))
	{
		g_pMediaStatus->recordStatus = 2;
		LogDebug("WARN: Second Video Chan %d is not config ok, disable second video recording\n", secondVideoChanId);
		strcpy(g_pMediaStatus->recordErrorMsg, "WARN: Second Video Chan param is not config ok");
		secondVideoChanId = 0;
	}

	set_encoder_two_stream_settings(firstVideoChanId, secondVideoChanId);

	enable_encoder(firstVideoChanId, true);
	start_encoder(firstVideoChanId);


	//更新状态图标
	showRecIcon(firstVideoChanId);
	if(secondVideoChanId > 0)showRecIcon(secondVideoChanId);
	if(enableAudio)showAudioRecIcon();

	g_pMediaStatus->recordStatus = 0;
	strcpy(g_pMediaStatus->recordErrorMsg, "Working");
	g_pMediaStatus->enableRecord = 1;
	LogDebug("StartRecord ok\n");
	return true;
}

void StopRecord()
{
	const STMediaConfig *pCfg = g_pMediaCfg;
	STRecordConfig stRecordCfg;
	memcpy(&stRecordCfg, &pCfg->recordCfg, sizeof(STRecordConfig));
	stRecordCfg.enableRecord = 0;

	Cfg_SetRecordConfig(&stRecordCfg);


	int firstVideoChanId = GetFirstVideoInputChanId(pCfg->recordCfg);
	if(firstVideoChanId > 0)
	{
		enable_encoder(firstVideoChanId, false);
		stop_encoder(firstVideoChanId);
	}

	//隐藏状态图标
	hideAudioRecIcon();
	hideRecIcon();

	g_pMediaStatus->enableRecord = 0;
	LogDebug("StopRecord ok\n");
}


void HideRecordStatusIcon()
{
	hideAudioRecIcon();
	hideRecIcon();
}



int  g_pip_vo_dev = -1;
extern int g_pip_vi_dev;

//增加文件
extern "C"
{
	struct STAddUploadFile
	{
		char fileName[256];
		char filePath[512];
		long long size;
	};

	#define CMD_ADD_UPLOAD_FILE 100

}



int main(int argc, char *argv[])
{
#ifdef _DEMONIZE
	//先fork 1次
	pid_t pid;
	pid = fork();
	if(pid)
	{//父进程先退出
		exit(0);
	}


REFORK:
	//再fork 第2次
	int status;
	pid = fork();
	if(pid)
	{//父进程,等待子进程，发现子进程退出就重新fork
		waitpid(pid, &status, 0);
		goto REFORK;
	}
	else{//子进程
#endif
		int ch;
		printf("hellow world\n");

#ifndef WIN32
		signal(SIGPIPE, SIG_IGN);  /* 向没有读进程的管道或socket写错误 */
		if(log_init("/mnt/recorder.log", 32) != 0)
		{
			printf("log_init failed\n");
			return -1;
		}
#endif
		FSL_LOG("\n\nrecoder V3.02\n");

		int i;
		qmsg_handler_t hMsgQ = NULL;
		char msgBuf[65000];
		qmsg_t *msg = (qmsg_t *)&msgBuf[0];



		STMediaConfig stMediaCfg;
		STMediaConfig *pCfg = &stMediaCfg;
		g_pMediaCfg = pCfg;


		if(msgq_get("web2recordQ", &hMsgQ) != 0)
		{
			printf("msgq_get web2recordQ failed!\n");
			return -1;
		}

		shm_handle_t hMemStatus = 0;
		sem_handle_t hSemStatus = 0;
		STMedaiStatus *pMediaStatus = NULL;
		int error = 0;
		//创建一块共享内存，与WebServr进程共享编解码的实时状态信息
		if(shm_get("recordStatusM", &hMemStatus, (void**)&pMediaStatus) != 0)
		{
			printf("shm_get('recordStatusM') failed! error:%s\n", shm_get_error());
			return -1;
		}


		g_pMediaStatus = pMediaStatus;
		//从共享内存中得到最新的配置，清0所有状态
		memcpy(g_pMediaCfg, &pMediaStatus->config, sizeof(STMediaConfig));
		memset(g_pMediaStatus, 0, sizeof(STMedaiStatus));
		//还原共享内存中的配置
		memcpy(&pMediaStatus->config, g_pMediaCfg, sizeof(STMediaConfig));

		g_inputVolume = g_pMediaCfg->audioCfg.volumeInput;
		g_inputMute = g_pMediaCfg->audioCfg.muteInput;

		if((error = sem_create("recordStatusSem", &hSemStatus)) != 0)
		{
			printf("sem_create('recordStatusSem') failed! error:%s\n", sem_get_error(error));
			return -1;
		}

		//查询系统时间
		bool bSyncOk = false;
		if(strlen(pCfg->staticIp.time_svr_ip) > 0)
		{
			int wc = 0;
			while(wc++ < 10 && !bSyncOk)
			{
				if(nc_query_update_system_time(pCfg->staticIp.time_svr_ip) != 0)
				{
				}
				else
				{
					bSyncOk = true;
					break;
				}
				usleep(999000);
			}

			FSL_LOG("Update syste time  from server:%s %s\n",pCfg->staticIp.time_svr_ip,  bSyncOk ? "sucess" : "failed");
			printf("Update syste time  from server:%s %s\n",pCfg->staticIp.time_svr_ip,  bSyncOk ? "sucess" : "failed");
		}




		signal(SIGPIPE, SIG_IGN);  /* 向没有读进程的管道或socket写错误 */

		//TestJsonRecordConf();
		init_encoder_env();
#ifdef TEST_BY_FILE
		ready_cap();
#else
		//hisi_init_cap();
#endif
		vid_set_vo_background_argb(0);
		CRenderBuilder::GetInstance()->SetRenderWndNum(4);
		StartAllVi2VoRender();

		if(openFB0() != 0)
		{
			printf("openFB0  failed, init OSD display layer failed\n");
			FSL_LOG("openFB0  failed, init OSD display layer failed\n");
		}

		SaveUnsavedRecordFile();

		//CRenderBuilder::GetInstance()->RenderHDMItoVo(1, get_pic_mod(pCfg->encoderArr[0].videoMode), pCfg->encoderArr[0].videoFps, pCfg->encoderArr[0].videoSrcFrameRate);
		//CRenderBuilder::GetInstance()->RenderHDMItoVo(2, get_pic_mod(pCfg->encoderArr[1].videoMode), pCfg->encoderArr[1].videoFps, pCfg->encoderArr[1].videoSrcFrameRate);


		//showRecIcon(1);
		//showAudioRecIcon();

		STEncoderItem *e;
		for(i = 0; i < pCfg->encoderNum; ++i)
		{
			e = &pCfg->encoderArr[i];
			//e->enableAudio = false;
			//e->enable = false;
			int vidMode;
			int srcFrameRate;
			if(get_hdmi_vi_info(e->videoInputChan, &vidMode, &srcFrameRate) == 0)
			{//获取实际的vi信息
				e->videoMode = vidMode;
				e->videoSrcFrameRate = srcFrameRate;
			}


			set_encoder_settings(e->videoInputChan,
				get_pic_mod(e->videoMode),
				e->videoFps,
				pCfg->recordCfg.videoCodecType,
				e->videoBitrate/*单位kbps*/,
				e->videoCodecType, e->szStreamName);
		}


		if(pCfg->recordCfg.enableRecord)
		{
			StartRecord();
		}

    pthread_t tid;
    if (0 != pthread_create(&tid, NULL, upgrade_proc, NULL))
	{
		fprintf(stderr, "%s\n","pthread_create failed");
	}

#if 0//单独测试 recorder
		//设置音视频捕获的参数
		set_encoder_settings(1,
			get_pic_mod(1)/*1 720P, 2 1080P*/,
			30,
			60,
			1500/*单位kbps*/,
			1/*1 h264, 2 jpeg*/,
			true/*twoVideoInOneStream*/,
			true/*enableAudio*/,
			16000/*audioSampleRate*/,
			2, 24/*audioBitrate 单位kbps*/, "test");


		//设置音视频捕获的参数
		set_encoder_settings(2,
			get_pic_mod(2)/*1 720P, 2 1080P*/,
			30,
			60,
			2000/*单位kbps*/,
			1/*1 h264, 2 jpeg*/,
			true/*twoVideoInOneStream*/,
			false/*enableAudio*/,
			0,
			0, 0/*audioBitrate 单位kbps*/, "test2");
		enable_encoder(1, true);
		start_encoder(1);


		while((ch = getchar()) != 'q')
		{
		}
#endif


		CPlayControlList * pcl = CPlayControlList::GetInstance();
		pcl->EnableSingleMode();
#if 0//单独测试解码器代码
		char *fileList[] = {"/mnt/disk1/1970-01-01_00H35M39S_551179.ts",
			"/mnt/disk1/1970-01-01_00H22M48S_408776.ts",
			"/mnt/disk1/1970-01-01_00H17M46S_735931.ts",
			"/mnt/disk1/1970-01-01_00H30M38S_956650.ts"
		};



		i = 0;
		while((ch = getchar()) != 'q')
		{
			if(ch == 'n')
			{
				CRenderBuilder::GetInstance()->StopRenderHDMItoVo(1);
				CRenderBuilder::GetInstance()->StopRenderHDMItoVo(2);
				char* url = fileList[i];
				++i;
				if( i == 4)i = 0;
				if(pcl->NewPlayer(url) != 0)
				{
					continue;
				}

				CNicePlayer *player = pcl->GetPlayer(url);
				player->Play();


				usleep(999000);
				usleep(999000);
				usleep(999000);
				usleep(999000);
				usleep(999000);
				printf("duration: %d.%dms\n", (int)(player->GetDuration() / 1000), (int)(player->GetDuration() % 1000));
				printf("current time: %d.%dms\n", (int)(player->GetCurrentPlayTime() / 1000), (int)(player->GetCurrentPlayTime() % 1000));

				player->Seek((int)(player->GetDuration() / 2));
				usleep(999000);
				printf("duration: %d.%dms\n", (int)(player->GetDuration() / 1000), (int)(player->GetDuration() % 1000));
				printf("current time: %d.%dms\n", (int)(player->GetCurrentPlayTime() / 1000), (int)(player->GetCurrentPlayTime() % 1000));

			}
		}
#endif


		//clear all cached msg
		while(msgq_recv(hMsgQ, msg, sizeof(msgBuf), 0, false) > 0)
		{
			usleep(1000);
		}

		FSL_LOG("\n\nStart recv msg from msgQ\n");
		while(1)
		{
			if(msgq_recv(hMsgQ, msg, sizeof(msgBuf), 0, false) > 0)
			{//处理消息
				if(msg->_type != MSG_QUERY_PLAY_STATUS)
				{
					FSL_LOG("\nmsgq_recv recv msg type = %d\n", msg->_type);
				}

				if(!bSyncOk && strlen(pCfg->staticIp.time_svr_ip) > 0)
				{//继续同步时间
					if(nc_query_update_system_time(pCfg->staticIp.time_svr_ip) == 0)
						bSyncOk = true;
				}

				switch(msg->_type)
				{
				case MSG_ADD_ENCODER:
				case MSG_MODIFY_ENCODER:
					{
						STEncoderItem *e = (STEncoderItem *)msg->_data;
						//e->enableAudio = false;
						//e->enable = false;



						set_encoder_settings(e->videoInputChan,
							get_pic_mod(e->videoMode),
							e->videoFps,
							e->videoSrcFrameRate,
							e->videoBitrate/*单位kbps*/,
							e->videoCodecType,
							e->szStreamName);

						//修改视频配置后vi2vo需要重启
						if(g_vi2voRenderStart)
						{
							StopAllVi2VoRender();
							StartAllVi2VoRender();
						}

					}
					break;
				case MSG_START_RECORD:
					{
						STRecordConfig stRecordCfg;
						STAudioConfig stAudioCfg;

						memcpy(&stRecordCfg, msg->_data, sizeof(STRecordConfig));
						memcpy(&stAudioCfg, msg->_data + sizeof(STRecordConfig), sizeof(STAudioConfig));
						int firstVideoChanId = GetFirstVideoInputChanId(stRecordCfg);

						if(memcmp(&stRecordCfg, &pCfg->recordCfg, sizeof(STRecordConfig)) == 0 && get_encoder_status(firstVideoChanId) == E_ENCODER_OK)
						{//配置未修改,并且录制状态正常
							pMediaStatus->msgProcStatus = 0;
							strcpy(pMediaStatus->msgLastError, "success");

							printf("MSG_START_RECORD do nothing\n");
							FSL_LOG("MSG_START_RECORD do nothing\n");
							break;
						}

						if(pCfg->recordCfg.enableRecord)
						{//先停止
							firstVideoChanId = GetFirstVideoInputChanId(pCfg->recordCfg);
							enable_encoder(firstVideoChanId, false);
							stop_encoder(firstVideoChanId);
							usleep(200000);
						}

						memcpy((void*)&pCfg->recordCfg, &stRecordCfg, sizeof(STRecordConfig));
						memcpy((void*)&pCfg->audioCfg, &stAudioCfg, sizeof(STAudioConfig));
						//修改视频编码类型
						for(i = 0; i < pCfg->encoderNum; ++i)
						{
							e = &pCfg->encoderArr[i];
							//e->enableAudio = false;
							//e->enable = false;
							int vidMode;
							int srcFrameRate;
							if(get_hdmi_vi_info(e->videoInputChan, &vidMode, &srcFrameRate) == 0)
							{//获取实际的vi信息
								e->videoMode = vidMode;
								e->videoSrcFrameRate = srcFrameRate;
							}


							set_encoder_settings(e->videoInputChan,
								get_pic_mod(e->videoMode),
								e->videoFps,
								e->videoSrcFrameRate,
								e->videoBitrate/*单位kbps*/,
								pCfg->recordCfg.videoCodecType, e->szStreamName);
						}

						if(!StartRecord())
						{
							pMediaStatus->msgProcStatus = pMediaStatus->recordStatus;
							strcpy(pMediaStatus->msgLastError, pMediaStatus->recordErrorMsg);
								printf("MSG_START_RECORD start failed\n");
						}
						else
						{
							pMediaStatus->msgProcStatus = 0;
							strcpy(pMediaStatus->msgLastError, "success");
							printf("MSG_START_RECORD start ok\n");
						}
					}
					break;
				case MSG_STOP_RECORD:
					{
						StopRecord();

						pMediaStatus->msgProcStatus = 0;
						strcpy(pMediaStatus->msgLastError, "success");
						break;
					}

				case MSG_REBOOT:
					{

						int firstVideoChanId = GetFirstVideoInputChanId(pCfg->recordCfg);
						if(firstVideoChanId > 0)
						{//停止录播,确保文本被正确保存
							stop_encoder(firstVideoChanId);
						}

						FSL_LOG("recv MSG_REBOOT, system rebooting ...\n\n\n");
						system("/sbin/reboot");
					}
					break;
				case MSG_QUERY_RECORD_FILE:
					{
						STQueryRecordFile stQueryCondition;
						memcpy(&stQueryCondition, msg->_data, sizeof(STQueryRecordFile));

						CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();

						int i;
						char szTmp[256];
						STRecordFile stFile;
						vector<STRecordFile> vFile;
						jsonConf->FindFileRecordByDataTime(stQueryCondition.startDate, stQueryCondition.startTime,
						  stQueryCondition.endDate,
							stQueryCondition.endTime, vFile);

						if(vFile.size() > 0 && strlen(stQueryCondition.fileName) > 0)
						{
							vector<STRecordFile> vFilterFile;
							jsonConf->FindFileRecordByName(vFile, stQueryCondition.fileName, vFilterFile);
							vFile = vFilterFile;
						}

						//保存找到的记录到临时文件
						CJsonRecordConf jsonTmpConf(stQueryCondition.confFileName);
						if(!jsonTmpConf.Init())
						{
							pMediaStatus->msgProcStatus = -1;
							strcpy(pMediaStatus->msgLastError, "jsonTmpConf.Init()  failed");
							printf("jsonTmpConf.Init()  failed\n");

							break ;
						}

						jsonTmpConf.GetFileRecords().insert(jsonTmpConf.GetFileRecords().end(),
							vFile.begin(), vFile.end());

						jsonTmpConf.SaveFile();

						pMediaStatus->msgProcStatus = 0;
						strcpy(pMediaStatus->msgLastError, "success");
					}
					break;
				case MSG_DEL_RECORD_FILE:
					{
						STDeleteRecordFile stDelCondition;
						memcpy(&stDelCondition, msg->_data, sizeof(stDelCondition));
						CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();



						CNicePlayer *player;
						STRecordFile stFile;
						if(jsonConf->GetFileItem(stDelCondition.fileEtag, stFile) &&
							(player = pcl->GetPlayer(stFile.fullPath.c_str())) != NULL)
						{//文件正在播放，关闭
							pcl->ClosePlayer(stFile.fullPath.c_str());
						}

						if(jsonConf->DelFileRecord(stDelCondition.fileEtag))
						{
							LogDebug("DelFileRecord %s ok\n", stDelCondition.fileEtag);
							strcpy(pMediaStatus->msgLastError, "DelFileRecord ok");
							pMediaStatus->msgProcStatus = 0;
						}
						else
						{
							strcpy(pMediaStatus->msgLastError, "DelFileRecord failed");
							pMediaStatus->msgProcStatus = -1;

						}

					}
					break;
				case MSG_DEL_SEL_FILE:
				{
						STCopyFile stCopyFile;
						CNicePlayer *player;
						memcpy(&stCopyFile, msg->_data, sizeof(stCopyFile));
						STRecordFile stFile;


						CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();
						//删除指定文件
						int n;
						for(n = 0; n < stCopyFile.fileNum; ++n)
						{
							if(jsonConf->GetFileItem(stCopyFile.etagArray[n], stFile))
							{
								if(stFile.lockFlag)//锁定文件，不删除，必须解锁后才可删除
									continue;

								if((player = pcl->GetPlayer(stFile.fullPath.c_str())) != NULL)
								{//文件正在播放，关闭
									pcl->ClosePlayer(stFile.fullPath.c_str());
								}

								jsonConf->DelFileRecord(stCopyFile.etagArray[n]);
							}
						}
						strcpy(pMediaStatus->msgLastError, "Del Sel File  ok");
						pMediaStatus->msgProcStatus = 0;
						break;
				}
				case MSG_FORMAT:
				{//格式化，删除所有录制文件
					CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();
					vector<STRecordFile>& vRecord = jsonConf->GetFileRecords();
					vector<STRecordFile>vCopy;
					vector<STRecordFile>::iterator itr;
					CNicePlayer *player;

					vCopy = vRecord;
					for(itr = vCopy.begin(); itr != vCopy.end(); itr++)
					{
						if((player = pcl->GetPlayer(itr->fullPath.c_str())) != NULL)
							{//文件正在播放，关闭
								pcl->ClosePlayer(itr->fullPath.c_str());
							}

						jsonConf->DelFileRecord(itr->etag.c_str());
					}

					FILE *fp = popen("/bin/rm -f /mnt/disk1/*.ts", "r");
					if(fp != NULL)
					{
						pclose(fp);
					}
					strcpy(pMediaStatus->msgLastError, "Del All File  ok");
					pMediaStatus->msgProcStatus = 0;

					break;
				}
				case MSG_DEL_OLDEST_FILE:
				{
					///break;//暂时屏蔽，测试用
					CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();
					CNicePlayer *player;
					vector<STRecordFile>& vRecord = jsonConf->GetFileRecords();
					if(vRecord.size() > 1)
					{//当前至少有2个文件
						STRecordFile stFile;
						int i;
						for(i = 0; i < (int)vRecord.size(); ++i)
						{
							if(!vRecord[i].lockFlag)//文件不是锁定状态,选择这个文件删除
							{
								stFile = vRecord[i];
								break;
							}
						}

						if(i < (int)vRecord.size())
						{
							if((player = pcl->GetPlayer(stFile.fullPath.c_str())) != NULL)
							{//文件正在播放，关闭
								pcl->ClosePlayer(stFile.fullPath.c_str());
							}

							if(jsonConf->DelFileRecord(stFile.etag))
							{
								LogDebug("MSG_DEL_OLDEST_FILE %s ok\n", stFile.fileName.c_str());
							}
						}
					}
					break;
				}

				case MSG_MODIFY_RECORD_FILE:
					{
						STModifyRecordFile stModifyFile;
						STRecordFile stFile;
						memcpy(&stModifyFile, msg->_data, sizeof(stModifyFile));
						CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();
						bool bOk = false;


						if(jsonConf->GetFileItem(stModifyFile.fileEtag, stFile))
						{
							if(stModifyFile.lockFlag != stFile.lockFlag)
							{//不是修改文件名，而是修改锁定标记
								stFile.lockFlag = stModifyFile.lockFlag;
								jsonConf->ModifyFileRecord(stFile);
								LogDebug("ModifyFileRecord %s ok\n", stModifyFile.fileEtag);
								strcpy(pMediaStatus->msgLastError, "ModifyFileRecord ok");
								pMediaStatus->msgProcStatus = 0;
								break;
							}

							string sNewFile = stFile.fullPath;
							struct stat st;
							sNewFile = sNewFile.replace(sNewFile.find(stFile.fileName), stFile.fileName.length(), stModifyFile.newFileName);

							if(stat(sNewFile.c_str(), &st) == 0)
							{
								strcpy(pMediaStatus->msgLastError, "File name existed");
								pMediaStatus->msgProcStatus = -1;
								break;
							}

							CNicePlayer *player;
							if((player = pcl->GetPlayer(stFile.fullPath.c_str())) != NULL)
							{//文件正在播放，关闭
								pcl->ClosePlayer(stFile.fullPath.c_str());
							}

							stFile.fileName = stModifyFile.newFileName;
							if(jsonConf->ModifyFileRecord(stFile))
							{
								bOk = true;
							}
						}


						if(bOk)
						{
							LogDebug("ModifyFileRecord %s ok\n", stModifyFile.fileEtag);
							strcpy(pMediaStatus->msgLastError, "ModifyFileRecord ok");
							pMediaStatus->msgProcStatus = 0;
						}
						else
						{
							strcpy(pMediaStatus->msgLastError, "ModifyFileRecord failed");
							pMediaStatus->msgProcStatus = -1;
						}
					}
					break;
				case MSG_PLAY:
					{
						STRecordFileKey stFileKey;
						memcpy(&stFileKey, msg->_data, sizeof(stFileKey));
						STRecordFile stFile;

						CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();

						if(jsonConf->GetFileItem(stFileKey.fileEtag, stFile))
						{
							CNicePlayer *player;
							int playStat;
							LogDebug(" play %s ready\n", stFile.fullPath.c_str());

							if((player = pcl->GetPlayer(stFile.fullPath.c_str())) == NULL)
							{
								if(pcl->NewPlayer(stFile.fullPath.c_str()) != 0)
								{
									strcpy(pMediaStatus->msgLastError, "Play file not found");
									pMediaStatus->msgProcStatus = -2;
									break;
								}

								player = pcl->GetPlayer(stFile.fullPath.c_str());
								player->SetVolume(pCfg->audioCfg.mute, pCfg->audioCfg.volume);//设置音量
								player->Play();
								LogDebug("Start new Player :%s ok\n", stFile.fullPath.c_str());
							}
							else
							{//暂停和播放状态互相切换
								int playStat =  player->GetPlayState();
								if(playStat == 1|| playStat == 2)
								{
									bool play = (playStat == 1 ? true : false);//暂停和播放状态互相切换
									player->Play(play);
								}
								else
								{//重新开始播放
									pcl->ClosePlayer(stFile.fullPath.c_str());

									if(pcl->NewPlayer(stFile.fullPath.c_str()) != 0)
									{
										strcpy(pMediaStatus->msgLastError, "Play file not found");
										pMediaStatus->msgProcStatus = -2;
										break;
									}

									player = pcl->GetPlayer(stFile.fullPath.c_str());
									player->Play();
									LogDebug("Start new Player :%s ok\n", stFile.fullPath.c_str());
								}
							}

						}
						else
						{
							strcpy(pMediaStatus->msgLastError, "Play file not found");
							pMediaStatus->msgProcStatus = -1;
							break;
						}

						strcpy(pMediaStatus->msgLastError, "play ok");
						pMediaStatus->msgProcStatus = 0;
						printf("play ok\n");


					}
					break;
				case MSG_SEEK:
					{
						STSeekRecordFile stFileKey;
						memcpy(&stFileKey, msg->_data, sizeof(stFileKey));
						STRecordFile stFile;

						CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();

						if(jsonConf->GetFileItem(stFileKey.fileEtag, stFile))
						{
							CNicePlayer *player;
							LogDebug(" MSG_SEEK %s ready\n", stFile.fullPath.c_str());

							if((player = pcl->GetPlayer(stFile.fullPath.c_str())) != NULL)
							{
								if(player->Seek(stFileKey.seekTime))
								{
									strcpy(pMediaStatus->msgLastError, "play seek ok");
									pMediaStatus->msgProcStatus = 0;
								}
								else
								{
									strcpy(pMediaStatus->msgLastError, "seek failed");
									pMediaStatus->msgProcStatus = -3;
								}


							}
							else
							{
								strcpy(pMediaStatus->msgLastError, "File is not  playing");
								printf("line %d File is not  playing\n", __LINE__);
								pMediaStatus->msgProcStatus = -2;
							}
						}
						else
						{
							strcpy(pMediaStatus->msgLastError, "Play file not found");
							pMediaStatus->msgProcStatus = -1;
						}
					}
					break;
				case MSG_SET_VOLUME:
					{//设置播放音量
						STVolume stVolume;
						memcpy(&stVolume, msg->_data, sizeof(stVolume));
						((STMediaConfig *)pCfg)->audioCfg.mute = stVolume.mute;
						((STMediaConfig *)pCfg)->audioCfg.volume = stVolume.volume;
						LogDebug(" MSG_SET_VOLUME mute:%d, volume:%d\n", stVolume.mute, stVolume.volume);

						CNicePlayer* player = pcl->GetCurrentPlay();
						if(player != NULL)
						{
							player->SetVolume(stVolume.mute, stVolume.volume);
						}
						strcpy(pMediaStatus->msgLastError, "MSG_SET_VOLUME ok");
						pMediaStatus->msgProcStatus = 0;
						break;
					}
				case MSG_SET_INPUT_VOLUME:
					{//设置播放音量
						STVolume stVolume;
						memcpy(&stVolume, msg->_data, sizeof(stVolume));
						((STMediaConfig *)pCfg)->audioCfg.muteInput = stVolume.mute;
						((STMediaConfig *)pCfg)->audioCfg.volumeInput = stVolume.volume;
						LogDebug(" MSG_SET_INPUT_VOLUME mute:%d, volume:%d\n", stVolume.mute, stVolume.volume);

						//设置编码器的音量或静音状态
						g_inputVolume = stVolume.volume;
						g_inputMute = stVolume.mute;

						strcpy(pMediaStatus->msgLastError, "MSG_SET_VOLUME ok");
						pMediaStatus->msgProcStatus = 0;
						break;
					}

				case MSG_STOP_PLAY:
					{
						STRecordFileKey stFileKey;
						memcpy(&stFileKey, msg->_data, sizeof(stFileKey));
						STRecordFile stFile;

						CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();

						if(jsonConf->GetFileItem(stFileKey.fileEtag, stFile))
						{
							CNicePlayer *player;
							LogDebug(" MSG_STOP_PLAY %s ready\n", stFile.fullPath.c_str());

							if((player = pcl->GetPlayer(stFile.fullPath.c_str())) != NULL)
							{
								pcl->ClosePlayer(stFile.fullPath.c_str());
								strcpy(pMediaStatus->msgLastError, "stop play ok");
								pMediaStatus->msgProcStatus = 0;
							}
							else
							{
								strcpy(pMediaStatus->msgLastError, "File is not  playing");
								printf("line %d File is not  playing\n", __LINE__);
								pMediaStatus->msgProcStatus = 0;
							}
						}
						else
						{
							strcpy(pMediaStatus->msgLastError, "Play file not found");
							pMediaStatus->msgProcStatus = -1;
						}
					}
					break;
				case MSG_QUERY_SWITCH_VIDEO:
					{
						STRecordFileKey stFileKey;
						memcpy(&stFileKey, msg->_data, sizeof(stFileKey));
						STRecordFile stFile;

						CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();

						if(jsonConf->GetFileItem(stFileKey.fileEtag, stFile))
						{
							CNicePlayer *player = NULL;
							//LogDebug(" MSG_QUERY_PLAY_STATUS %s ready\n", stFile.fullPath.c_str());
							player = pcl->GetPlayer(stFile.fullPath.c_str());

							if(player != NULL)
							{
								player->SwitchTwoVideo();
								strcpy(pMediaStatus->msgLastError, "Switch video ok");
								pMediaStatus->msgProcStatus = 0;
							}
							else
							{
								strcpy(pMediaStatus->msgLastError, "File is not  playing");
								printf("line %d File is not  playing, player=%p\n", __LINE__, player);
								pMediaStatus->msgProcStatus = -2;
							}
						}
						else
						{
							strcpy(pMediaStatus->msgLastError, "Play file not found");
							pMediaStatus->msgProcStatus = -1;
						}
					}
					break;

				case MSG_SHOW_SMALL_VIDEO:
					{
						int showFlag;
						memcpy(&showFlag, msg->_data, sizeof(int));
						CNicePlayer* player = pcl->GetCurrentPlay();
						if(player != NULL)
						{
							player->ShowSmallWnd(showFlag ? true : false);
							strcpy(pMediaStatus->msgLastError, "ShowSmallVideo ok");
							pMediaStatus->msgProcStatus = 0;
						}
						else
						{
							strcpy(pMediaStatus->msgLastError, "Player not do  playing");
							printf("line %d File is not  playing, player=%p\n", __LINE__, player);
							pMediaStatus->msgProcStatus = -2;
						}
					}
					break;
				case MSG_QUERY_PLAY_STATUS:
					{
						STRecordFileKey stFileKey;
						memcpy(&stFileKey, msg->_data, sizeof(stFileKey));
						STRecordFile stFile;

						CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();

						if(jsonConf->GetFileItem(stFileKey.fileEtag, stFile))
						{
							CNicePlayer *player = NULL;
							//LogDebug(" MSG_QUERY_PLAY_STATUS %s ready\n", stFile.fullPath.c_str());
							player = pcl->GetPlayer(stFile.fullPath.c_str());

							if(player != NULL)
							{
								char szMsg[256];

								int n = snprintf(szMsg, 250, "{\"etag\":\"%s\", \"current_play_time\":%d, \"play_status\":%d, \"duration\": %d, \"video_num\":%d, \"mute\":%d, \"volume\":%d, \"show_small_wnd\":%d}",
									stFileKey.fileEtag,  (int)player->GetCurrentPlayTime(),
									player->GetPlayState(), (int)(player->GetDuration()),
									player->GetVideoStreamNum(),
									pCfg->audioCfg.mute, pCfg->audioCfg.volume, player->IsShowSmallWnd() ? 1 : 0);
								memcpy(pMediaStatus->msgLastError, szMsg, n + 1);
								pMediaStatus->msgProcStatus = 0;
							}
							else
							{
								strcpy(pMediaStatus->msgLastError, "File is not  playing");
								printf("line %d File is not  playing, player=%p, stFile.fullPath.c_str()=%s\n", __LINE__, player, stFile.fullPath.c_str());
								pMediaStatus->msgProcStatus = -2;
							}
						}
						else
						{
							strcpy(pMediaStatus->msgLastError, "Play file not found");
							pMediaStatus->msgProcStatus = -1;
						}
					}
					//printf("MSG_QUERY_PLAY_STATUS : msgProcStatus:%d\n", pMediaStatus->msgProcStatus);
					break;
				case MSG_COPY_FILE:
					{
						STCopyFile stCopyFile;
						memcpy(&stCopyFile, msg->_data, sizeof(stCopyFile));
						STRecordFile stFile;
						vector<STRecordFile> *pvRecordFile = new vector<STRecordFile>();
						vector<STRecordFile>&vRecordFile = *pvRecordFile;

						CJsonRecordConf* jsonConf = CJsonRecordConf::GetInstance();
						if(stCopyFile.fileNum == 0)
						{//拷贝全部文件
							jsonConf->FindFileRecordByDataTime("", "", "", "", vRecordFile);
						}
						else
						{//拷贝指定文件
							int n;
							for(n = 0; n < stCopyFile.fileNum; ++n)
							{
								if(jsonConf->GetFileItem(stCopyFile.etagArray[n], stFile))
								{
									vRecordFile.push_back(stFile);
								}
							}
						}


						if(StartCopyFile(stCopyFile.dstPath, *pvRecordFile) == 0)
						{
							strcpy(pMediaStatus->msgLastError, "Start copy file ok");
							pMediaStatus->msgProcStatus = 0;
						}
						else
						{
							strcpy(pMediaStatus->msgLastError, "Start copy file failed");
							pMediaStatus->msgProcStatus = -1;
						}
					}
					break;
				case MSG_GET_COPY_FILE_STATUS:
					{
						STCopyStatus status;
						string sLastError;
						string sCurFile;
						GetCopyFileStatus(status, sCurFile, sLastError);
						char szProgressTotal[128];
						char szProgressCurFile[256];
						int state = -1;

						if(status.fileNum <= 0)
						{
							sprintf(szProgressTotal, "拷贝进度100%c, 共0个文件", '%');
							strcpy(szProgressCurFile, "");
							state = 0;
							sLastError = "未拷贝任何文件";
						}
						else
						{
							if(status.doneSize == status.totalSize)
							{
								sLastError = "拷贝完成";
								state = 0;
							}
							else
							{
								if(status.cancelCopy && status.copyDone)
								{//用户取消拷贝，且取消完毕
									state = 0;
								}
								else
								{
									state = 1;
								}
							}
							if(status.totalSize == 0)status.totalSize = 1;
							sprintf(szProgressTotal, "拷贝总进度%d%c, 拷贝第%d个文件/共%d个文件，已拷贝%dG%dM/共%dG%dM",
								(int)(100 * status.doneSize / status.totalSize), '%',
								status.doneFileNum < status.fileNum ? status.doneFileNum + 1 : status.fileNum, status.fileNum,
								(int)(status.doneSize >> 30), (int)((status.doneSize % (1LL << 30)) >> 20),
								(int)(status.totalSize >> 30), (int)((status.totalSize % (1LL << 30)) >> 20));

							if(status.curFileSize == 0)status.curFileSize = 1;
							sprintf(szProgressCurFile, "当前拷贝文件:%s, 进度%d%c, 已拷贝%dG%dM/共%dG%dM", sCurFile.c_str(),
									(int)(100 * status.curFileDoneSize / status.curFileSize), '%',
									(int)(status.curFileDoneSize >> 30), (int)((status.curFileDoneSize % (1LL << 30)) >> 20),
									(int)(status.curFileSize >> 30), (int)((status.curFileSize % (1LL << 30)) >> 20));

						}

						sprintf(pMediaStatus->msgLastError, "{state:%d, totalProgress:\"%s\", curFileProgress:\"%s\", msg:\"%s\"}", state,
							szProgressTotal, szProgressCurFile, sLastError.c_str());

						pMediaStatus->msgProcStatus = 0;
					}
					break;
				case MSG_CANCEL_COPY_FILE:
					{
						CancelCopyFile();
						strcpy(pMediaStatus->msgLastError, "Success");
						pMediaStatus->msgProcStatus = 0;
					}
					break;
				case MSG_CLEAR_COPY_INFO:
					{
						ClearCopyInfo();
						strcpy(pMediaStatus->msgLastError, "Success");
						pMediaStatus->msgProcStatus = 0;
						break;
					}
				case 	CMD_ADD_UPLOAD_FILE:
				{
					STAddUploadFile *fileInfo = (STAddUploadFile *)msg->_data;
					STRecordFile stFile;
					char szTmp[128];

					CFileDemuxer fileDemuxer(fileInfo->filePath);
					stFile.fileName = fileInfo->fileName;
					stFile.fullPath = fileInfo->filePath;
					stFile.size = fileInfo->size;

					int duration = 0;
					if(fileDemuxer.Open() != 0)
					{
						LogError("fileDemuxer.Open %s failed\n", fileInfo->filePath);
					}
					else
					{
						int64_t timeLen = fileDemuxer.GetDuration();
						timeLen /= 1000000LL;

						duration = (int)(timeLen);//转换单位秒
					}

					time_t now ;
				  struct tm tm_now;


				  time(&now) ;
				  localtime_r(&now, &tm_now) ;


					sprintf(szTmp, "%d-%d-%d",
					   tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday);
					stFile.date = szTmp;

					stFile.time = duration;


					sprintf(szTmp, "%d:%d:%d",
	     						tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
					stFile.startTime =  szTmp;
					stFile.endTime = szTmp;

					if(!CJsonRecordConf::GetInstance()->AddFileRecord(stFile))
					{
						LogError("AddFileRecord for add update file failed\n");
					}
					else
					{
						LogDebug("AddFileRecord for add update file ok, file:%s\n", fileInfo->fileName);
					}

					break;
				}


				default:
					break;
				}
			}
			else
			{
				//监听控制面板发来的网络命令
				net_control_service_proc();
				pcl->CheckPlayerState();


				sem_lock(hSemStatus);

				//更新录制状态
				int firstVideoId = GetFirstVideoInputChanId(pCfg->recordCfg);
				if(firstVideoId > 0 && pCfg->recordCfg.enableRecord)
				{
					char szMsg[128];
					int status = get_encoder_status(firstVideoId);
					if(status >= E_ENCODER_OK)
					{
						g_pMediaStatus->recordStatus = (status == E_ENCODER_OK ? 0 : status);
						strcpy(g_pMediaStatus->recordErrorMsg, get_encoder_error(status));
					}

					status = CCameraControler::getInstance().getCameraStatusEx(firstVideoId);
					if(status & E_ENCODER_VIDEO_NO_DATA)
					{
						sprintf(szMsg, ", 通道%d无视频输入信号", firstVideoId);
						strcat(g_pMediaStatus->recordErrorMsg, szMsg);
					}

					if(status & E_ENCODER_VIDEO2_NO_DATA)
					{
						sprintf(szMsg, ", 通道%d无视频输入信号", GetSecondVideoInputChanId(pCfg->recordCfg));
						strcat(g_pMediaStatus->recordErrorMsg, szMsg);
					}


					if(status == E_ENCODER_OPEN_AUDIO_DEV_FAIL || status == E_ENCODER_OPEN_VIDEO_DEV_FAIL)
					{
						FSL_LOG("\n\nFatal encoder error:%d, must abort process\n",status);
						goto END1;
					}
				}


				sem_unlock(hSemStatus);

				static int getDiskSpaceTimerCount = 1;
				if(getDiskSpaceTimerCount++ % 100 == 0)
				{
					STDirInfo stDirInfo;

					if(GetDirInfo("/mnt/disk1", &stDirInfo) && stDirInfo.leftSize < 30)
					{
						FSL_LOG("No disk space, disk left size:%d\n", stDirInfo.leftSize);
						if(firstVideoId > 0 && pCfg->recordCfg.enableRecord)
						{
							stop_encoder(firstVideoId);
							usleep(200000);
							CCameraControler::getInstance().setCameraStatus(firstVideoId, E_ENCODER_NO_DISK_SPACE);
						}
					}
				}

				if(getDiskSpaceTimerCount % 18000 == 0)
				{//大概3600秒，1个小时，时间同步一次
					//查询系统时间
					if(strlen(pCfg->staticIp.time_svr_ip) > 0)
					{
						bool bOk = false;
						if(nc_query_update_system_time(pCfg->staticIp.time_svr_ip) == 0)
						{
							bOk = true;
							if(getDiskSpaceTimerCount < 20000 && getDiskSpaceTimerCount > 0)
							{
								FSL_LOG("Update syste time  from server:%s %s\n",pCfg->staticIp.time_svr_ip,  bOk ? "sucess" : "failed");
							}
						}

						if(!bOk)FSL_LOG("Update syste time  from server:%s %s\n",pCfg->staticIp.time_svr_ip,  bOk ? "sucess" : "failed");
					}
				}
			}
		}


END1:



		shmdt(pMediaStatus);//释放状态共享内存



		//stop_encoder(1);
		usleep(999000);
		free_encoder_env();

		hisi_exit();
#ifdef _DEMONIZE
	}//end 子进程逻辑
#endif
	return 0;
}

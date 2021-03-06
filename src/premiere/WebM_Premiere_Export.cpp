///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2013, Brendan Bolles
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *	   Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *	   Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------
//
// WebM plug-in for Premiere
//
// by Brendan Bolles <brendan@fnordware.com>
//
// ------------------------------------------------------------------------



#include "WebM_Premiere_Export.h"

#include "WebM_Premiere_Export_Params.h"


#ifdef PRMAC_ENV
	#include <mach/mach.h>
#else
	#include <assert.h>
	#include <time.h>
	#include <sys/timeb.h>
#endif


extern "C" {

#include "vpx/vpx_encoder.h"
#include "vpx/vp8cx.h"

#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

}

#include "mkvmuxer.hpp"



using mkvmuxer::uint8;
using mkvmuxer::int32;
using mkvmuxer::uint32;
using mkvmuxer::int64;
using mkvmuxer::uint64;

class PrMkvWriter : public mkvmuxer::IMkvWriter
{
  public:
	PrMkvWriter(PrSDKExportFileSuite *fileSuite, csSDK_uint32 fileObject);
	virtual ~PrMkvWriter();
	
	virtual int32 Write(const void* buf, uint32 len);
	virtual int64 Position() const;
	virtual int32 Position(int64 position); // seek
	virtual bool Seekable() const { return true; }
	virtual void ElementStartNotify(uint64 element_id, int64 position);
	
  private:
	const PrSDKExportFileSuite *_fileSuite;
	const csSDK_uint32 _fileObject;
	
};

PrMkvWriter::PrMkvWriter(PrSDKExportFileSuite *fileSuite, csSDK_uint32 fileObject) :
	_fileSuite(fileSuite),
	_fileObject(fileObject)
{
	prSuiteError err = _fileSuite->Open(_fileObject);
	
	if(err != malNoError)
		throw err;
}

PrMkvWriter::~PrMkvWriter()
{
	prSuiteError err = _fileSuite->Close(_fileObject);
}

int32
PrMkvWriter::Write(const void* buf, uint32 len)
{
	prSuiteError err = _fileSuite->Write(_fileObject, (void *)buf, len);
	
	return err;
}


int64
PrMkvWriter::Position() const
{
	prInt64 pos = 0;

// son of a gun, fileSeekMode_End and fileSeekMode_Current are flipped inside Premiere!
#define PR_SEEK_CURRENT fileSeekMode_End

	prSuiteError err = _fileSuite->Seek(_fileObject, 0, pos, PR_SEEK_CURRENT);
	
	return pos;
}

int32
PrMkvWriter::Position(int64 position)
{
	prInt64 pos = 0;

	prSuiteError err = _fileSuite->Seek(_fileObject, position, pos, fileSeekMode_Begin);
	
	return err;
}

void
PrMkvWriter::ElementStartNotify(uint64 element_id, int64 position)
{
	// ummm, should I do something?
}


#pragma mark-


static const csSDK_int32 WebM_ID = 'WebM';
static const csSDK_int32 WebM_Export_Class = 'WebM';

extern int g_num_cpus;



static void
utf16ncpy(prUTF16Char *dest, const char *src, int max_len)
{
	prUTF16Char *d = dest;
	const char *c = src;
	
	do{
		*d++ = *c;
	}while(*c++ != '\0' && --max_len);
}

static void
ncpyUTF16(char *dest, const prUTF16Char *src, int max_len)
{
	char *d = dest;
	const prUTF16Char *c = src;
	
	do{
		*d++ = *c;
	}while(*c++ != '\0' && --max_len);
}

static prMALError
exSDKStartup(
	exportStdParms		*stdParmsP, 
	exExporterInfoRec	*infoRecP)
{
	PrSDKAppInfoSuite *appInfoSuite = NULL;
	stdParmsP->getSPBasicSuite()->AcquireSuite(kPrSDKAppInfoSuite, kPrSDKAppInfoSuiteVersion, (const void**)&appInfoSuite);
	
	if(appInfoSuite)
	{
		int fourCC = 0;
	
		appInfoSuite->GetAppInfo(PrSDKAppInfoSuite::kAppInfo_AppFourCC, (void *)&fourCC);
	
		stdParmsP->getSPBasicSuite()->ReleaseSuite(kPrSDKAppInfoSuite, kPrSDKAppInfoSuiteVersion);
		
		// not a good idea to try to run a MediaCore exporter in AE
		if(fourCC == kAppAfterEffects)
			return exportReturn_IterateExporterDone;
	}
	

	infoRecP->fileType			= WebM_ID;
	
	utf16ncpy(infoRecP->fileTypeName, "WebM", 255);
	utf16ncpy(infoRecP->fileTypeDefaultExtension, "webm", 255);
	
	infoRecP->classID = WebM_Export_Class;
	
	infoRecP->exportReqIndex	= 0;
	infoRecP->wantsNoProgressBar = kPrFalse;
	infoRecP->hideInUI			= kPrFalse;
	infoRecP->doesNotSupportAudioOnly = kPrFalse;
	infoRecP->canExportVideo	= kPrTrue;
	infoRecP->canExportAudio	= kPrTrue;
	infoRecP->singleFrameOnly	= kPrFalse;
	
	infoRecP->interfaceVersion	= EXPORTMOD_VERSION;
	
	infoRecP->isCacheable		= kPrFalse;
	

	return malNoError;
}


static prMALError
exSDKBeginInstance(
	exportStdParms			*stdParmsP, 
	exExporterInstanceRec	*instanceRecP)
{
	prMALError				result				= malNoError;
	SPErr					spError				= kSPNoError;
	ExportSettings			*mySettings;
	PrSDKMemoryManagerSuite	*memorySuite;
	csSDK_int32				exportSettingsSize	= sizeof(ExportSettings);
	SPBasicSuite			*spBasic			= stdParmsP->getSPBasicSuite();
	
	if(spBasic != NULL)
	{
		spError = spBasic->AcquireSuite(
			kPrSDKMemoryManagerSuite,
			kPrSDKMemoryManagerSuiteVersion,
			const_cast<const void**>(reinterpret_cast<void**>(&memorySuite)));
			
		mySettings = reinterpret_cast<ExportSettings *>(memorySuite->NewPtrClear(exportSettingsSize));

		if(mySettings)
		{
			mySettings->spBasic		= spBasic;
			mySettings->memorySuite	= memorySuite;
			
			spError = spBasic->AcquireSuite(
				kPrSDKExportParamSuite,
				kPrSDKExportParamSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportParamSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKExportFileSuite,
				kPrSDKExportFileSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportFileSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKExportInfoSuite,
				kPrSDKExportInfoSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportInfoSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKExportProgressSuite,
				kPrSDKExportProgressSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->exportProgressSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKPPixCreatorSuite,
				kPrSDKPPixCreatorSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->ppixCreatorSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKPPixSuite,
				kPrSDKPPixSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->ppixSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKPPix2Suite,
				kPrSDKPPix2SuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->ppix2Suite))));
			spError = spBasic->AcquireSuite(
				kPrSDKSequenceRenderSuite,
				kPrSDKSequenceRenderSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->sequenceRenderSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKSequenceAudioSuite,
				kPrSDKSequenceAudioSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->sequenceAudioSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKTimeSuite,
				kPrSDKTimeSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->timeSuite))));
			spError = spBasic->AcquireSuite(
				kPrSDKWindowSuite,
				kPrSDKWindowSuiteVersion,
				const_cast<const void**>(reinterpret_cast<void**>(&(mySettings->windowSuite))));
		}


		instanceRecP->privateData = reinterpret_cast<void*>(mySettings);
	}
	else
	{
		result = exportReturn_ErrMemory;
	}
	
	return result;
}


static prMALError
exSDKEndInstance(
	exportStdParms			*stdParmsP, 
	exExporterInstanceRec	*instanceRecP)
{
	prMALError				result		= malNoError;
	ExportSettings			*lRec		= reinterpret_cast<ExportSettings *>(instanceRecP->privateData);
	SPBasicSuite			*spBasic	= stdParmsP->getSPBasicSuite();
	PrSDKMemoryManagerSuite	*memorySuite;
	if(spBasic != NULL && lRec != NULL)
	{
		if (lRec->exportParamSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportParamSuite, kPrSDKExportParamSuiteVersion);
		}
		if (lRec->exportFileSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportFileSuite, kPrSDKExportFileSuiteVersion);
		}
		if (lRec->exportInfoSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportInfoSuite, kPrSDKExportInfoSuiteVersion);
		}
		if (lRec->exportProgressSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKExportProgressSuite, kPrSDKExportProgressSuiteVersion);
		}
		if (lRec->ppixCreatorSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKPPixCreatorSuite, kPrSDKPPixCreatorSuiteVersion);
		}
		if (lRec->ppixSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKPPixSuite, kPrSDKPPixSuiteVersion);
		}
		if (lRec->ppix2Suite)
		{
			result = spBasic->ReleaseSuite(kPrSDKPPix2Suite, kPrSDKPPix2SuiteVersion);
		}
		if (lRec->sequenceRenderSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKSequenceRenderSuite, kPrSDKSequenceRenderSuiteVersion);
		}
		if (lRec->sequenceAudioSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKSequenceAudioSuite, kPrSDKSequenceAudioSuiteVersion);
		}
		if (lRec->timeSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKTimeSuite, kPrSDKTimeSuiteVersion);
		}
		if (lRec->windowSuite)
		{
			result = spBasic->ReleaseSuite(kPrSDKWindowSuite, kPrSDKWindowSuiteVersion);
		}
		if (lRec->memorySuite)
		{
			memorySuite = lRec->memorySuite;
			memorySuite->PrDisposePtr(reinterpret_cast<PrMemoryPtr>(lRec));
			result = spBasic->ReleaseSuite(kPrSDKMemoryManagerSuite, kPrSDKMemoryManagerSuiteVersion);
		}
	}

	return result;
}



static prMALError
exSDKFileExtension(
	exportStdParms					*stdParmsP, 
	exQueryExportFileExtensionRec	*exportFileExtensionRecP)
{
	utf16ncpy(exportFileExtensionRecP->outFileExtension, "webm", 255);
		
	return malNoError;
}


static void get_framerate(PrTime ticksPerSecond, PrTime ticks_per_frame, exRatioValue *fps)
{
	PrTime frameRates[] = {	10, 15, 23,
							24, 25, 29,
							30, 50, 59,
							60};
													
	PrTime frameRateNumDens[][2] = {{10, 1}, {15, 1}, {24000, 1001},
									{24, 1}, {25, 1}, {30000, 1001},
									{30, 1}, {50, 1}, {60000, 1001},
									{60, 1}};
	
	int frameRateIndex = -1;
	
	for(csSDK_int32 i=0; i < sizeof(frameRates) / sizeof (PrTime); i++)
	{
		frameRates[i] = ticksPerSecond / frameRateNumDens[i][0] * frameRateNumDens[i][1];
		
		if(ticks_per_frame == frameRates[i])
			frameRateIndex = i;
	}
	
	if(frameRateIndex >= 0)
	{
		fps->numerator = frameRateNumDens[frameRateIndex][0];
		fps->denominator = frameRateNumDens[frameRateIndex][1];
	}
	else
	{
		fps->numerator = 1000 * ticksPerSecond / ticks_per_frame;
		fps->denominator = 1000;
	}
}


// converting from the Adobe 16-bit, i.e. max_val is 0x8000
static inline unsigned char
Convert16to8(const unsigned short &v)
{
	return ( (((long)(v) * 255) + 16384) / 32768);
}


static int
xiph_len(int l)
{
    return 1 + l / 255 + l;
}

static void
xiph_lace(unsigned char **np, uint64_t val)
{
	unsigned char *p = *np;

	while(val >= 255)
	{
		*p++ = 255;
		val -= 255;
	}
	
	*p++ = val;
	
	*np = p;
}

static void *
MakePrivateData(ogg_packet &header, ogg_packet &header_comm, ogg_packet &header_code, size_t &size)
{
	size = 1 + xiph_len(header.bytes) + xiph_len(header_comm.bytes) + header_code.bytes;
	
	void *buf = malloc(size);
	
	if(buf)
	{
		unsigned char *p = (unsigned char *)buf;
		
		*p++ = 2;
		
		xiph_lace(&p, header.bytes);
		xiph_lace(&p, header_comm.bytes);
		
		memcpy(p, header.packet, header.bytes);
		p += header.bytes;
		memcpy(p, header_comm.packet, header_comm.bytes);
		p += header_comm.bytes;
		memcpy(p, header_code.packet, header_code.bytes);
	}
	
	return buf;
}


static prMALError
exSDKExport(
	exportStdParms	*stdParmsP,
	exDoExportRec	*exportInfoP)
{
	prMALError					result					= malNoError;
	ExportSettings				*mySettings				= reinterpret_cast<ExportSettings*>(exportInfoP->privateData);
	PrSDKExportParamSuite		*paramSuite				= mySettings->exportParamSuite;
	PrSDKExportInfoSuite		*exportInfoSuite		= mySettings->exportInfoSuite;
	PrSDKSequenceRenderSuite	*renderSuite			= mySettings->sequenceRenderSuite;
	PrSDKSequenceAudioSuite		*audioSuite				= mySettings->sequenceAudioSuite;
	PrSDKMemoryManagerSuite		*memorySuite			= mySettings->memorySuite;
	PrSDKPPixCreatorSuite		*pixCreatorSuite		= mySettings->ppixCreatorSuite;
	PrSDKPPixSuite				*pixSuite				= mySettings->ppixSuite;
	PrSDKPPix2Suite				*pix2Suite				= mySettings->ppix2Suite;


	PrTime ticksPerSecond = 0;
	mySettings->timeSuite->GetTicksPerSecond(&ticksPerSecond);
	
	
	csSDK_uint32 exID = exportInfoP->exporterPluginID;
	csSDK_int32 gIdx = 0;
	
	exParamValues matchSourceP, widthP, heightP, pixelAspectRatioP, fieldTypeP, frameRateP;
	
	paramSuite->GetParamValue(exID, gIdx, ADBEVideoMatchSource, &matchSourceP);
	
	if(matchSourceP.value.intValue)
	{
		// get current settings
		PrParam curr_widthP, curr_heightP, curr_parN, curr_parD, curr_fieldTypeP, curr_frameRateP;
		
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_VideoWidth, &curr_widthP);
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_VideoHeight, &curr_heightP);
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_PixelAspectNumerator, &curr_parN);
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_PixelAspectDenominator, &curr_parD);
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_VideoFieldType, &curr_fieldTypeP);
		exportInfoSuite->GetExportSourceInfo(exID, kExportInfo_VideoFrameRate, &curr_frameRateP);
		
		widthP.value.intValue = curr_widthP.mInt32;
		heightP.value.intValue = curr_heightP.mInt32;
		
		pixelAspectRatioP.value.ratioValue.numerator = curr_parN.mInt32;
		pixelAspectRatioP.value.ratioValue.denominator = curr_parD.mInt32;
		
		fieldTypeP.value.intValue = curr_fieldTypeP.mInt32;
		frameRateP.value.timeValue = curr_frameRateP.mInt64;
	}
	else
	{
		paramSuite->GetParamValue(exID, gIdx, ADBEVideoWidth, &widthP);
		paramSuite->GetParamValue(exID, gIdx, ADBEVideoHeight, &heightP);
		paramSuite->GetParamValue(exID, gIdx, ADBEVideoAspect, &pixelAspectRatioP);
		paramSuite->GetParamValue(exID, gIdx, ADBEVideoFieldType, &fieldTypeP);
		paramSuite->GetParamValue(exID, gIdx, ADBEVideoFPS, &frameRateP);
	}
	
	exParamValues alphaP;
	//paramSuite->GetParamValue(exID, gIdx, ADBEVideoAlpha, &alphaP);
	alphaP.value.intValue = 0;
	
	exParamValues sampleRateP, channelTypeP;
	paramSuite->GetParamValue(exID, gIdx, ADBEAudioRatePerSecond, &sampleRateP);
	paramSuite->GetParamValue(exID, gIdx, ADBEAudioNumChannels, &channelTypeP);
	
	const PrAudioChannelType audioFormat = (PrAudioChannelType)channelTypeP.value.intValue;
	const int audioChannels = (audioFormat == kPrAudioChannelType_51 ? 6 :
								audioFormat == kPrAudioChannelType_Mono ? 1 :
								2);
	
	exParamValues codecP, methodP, videoQualityP, bitrateP, vidEncodingP, customArgsP;
	paramSuite->GetParamValue(exID, gIdx, WebMVideoCodec, &codecP);
	paramSuite->GetParamValue(exID, gIdx, WebMVideoMethod, &methodP);
	paramSuite->GetParamValue(exID, gIdx, WebMVideoQuality, &videoQualityP);
	paramSuite->GetParamValue(exID, gIdx, WebMVideoBitrate, &bitrateP);
	paramSuite->GetParamValue(exID, gIdx, WebMVideoEncoding, &vidEncodingP);
	paramSuite->GetParamValue(exID, gIdx, WebMCustomArgs, &customArgsP);
	
	WebM_Video_Method method = (WebM_Video_Method)methodP.value.intValue;
	
	char customArgs[256];
	ncpyUTF16(customArgs, customArgsP.paramString, 255);
	customArgs[255] = '\0';
	

	exParamValues audioMethodP, audioQualityP, audioBitrateP;
	paramSuite->GetParamValue(exID, gIdx, WebMAudioMethod, &audioMethodP);
	paramSuite->GetParamValue(exID, gIdx, WebMAudioQuality, &audioQualityP);
	paramSuite->GetParamValue(exID, gIdx, WebMAudioBitrate, &audioBitrateP);
	
	
	SequenceRender_ParamsRec renderParms;
	PrPixelFormat pixelFormats[] = { PrPixelFormat_YUV_420_MPEG2_FRAME_PICTURE_PLANAR_8u_709,
									PrPixelFormat_BGRA_4444_16u, // must support BGRA, even if I don't want to
									PrPixelFormat_BGRA_4444_8u };
	
	renderParms.inRequestedPixelFormatArray = pixelFormats;
	renderParms.inRequestedPixelFormatArrayCount = 3;
	renderParms.inWidth = widthP.value.intValue;
	renderParms.inHeight = heightP.value.intValue;
	renderParms.inPixelAspectRatioNumerator = pixelAspectRatioP.value.ratioValue.numerator;
	renderParms.inPixelAspectRatioDenominator = pixelAspectRatioP.value.ratioValue.denominator;
	renderParms.inRenderQuality = kPrRenderQuality_High;
	renderParms.inFieldType = fieldTypeP.value.intValue;
	renderParms.inDeinterlace = kPrFalse;
	renderParms.inDeinterlaceQuality = kPrRenderQuality_High;
	renderParms.inCompositeOnBlack = (alphaP.value.intValue ? kPrFalse: kPrTrue);
	
	
	csSDK_uint32 videoRenderID = 0;
	
	if(exportInfoP->exportVideo)
	{
		result = renderSuite->MakeVideoRenderer(exID, &videoRenderID, frameRateP.value.timeValue);
	}
	
	csSDK_uint32 audioRenderID = 0;
	
	if(exportInfoP->exportAudio)
	{
		result = audioSuite->MakeAudioRenderer(exID,
												exportInfoP->startTime,
												audioFormat,
												kPrAudioSampleType_32BitFloat,
												sampleRateP.value.floatValue, 
												&audioRenderID);
	}

	
	PrMemoryPtr vbr_buffer = NULL;
	size_t vbr_buffer_size = 0;


	try{
	
	const int passes = ( (exportInfoP->exportVideo && method == WEBM_METHOD_VBR) ? 2 : 1);
	
	for(int pass = 0; pass < passes && result == malNoError; pass++)
	{
		const bool vbr_pass = (passes > 1 && pass == 0);
		

		if(passes > 1)
		{
			prUTF16Char utf_str[256];
		
			if(vbr_pass)
				utf16ncpy(utf_str, "Analyzing video", 255);
			else
				utf16ncpy(utf_str, "Encoding WebM movie", 255);
			
			// This doesn't seem to be doing anything
			mySettings->exportProgressSuite->SetProgressString(exID, utf_str);
		}
		
		
	
		exRatioValue fps;
		get_framerate(ticksPerSecond, frameRateP.value.timeValue, &fps);
		
		
		vpx_codec_err_t codec_err = VPX_CODEC_OK;
		
		vpx_codec_ctx_t encoder;
		
		if(exportInfoP->exportVideo)
		{
			vpx_codec_iface_t *iface = codecP.value.intValue == WEBM_CODEC_VP9 ? vpx_codec_vp9_cx() :
										vpx_codec_vp8_cx();
			
			vpx_codec_enc_cfg_t config;
			vpx_codec_enc_config_default(iface, &config, 0);
			
			config.g_w = renderParms.inWidth;
			config.g_h = renderParms.inHeight;
			
			
			if(method == WEBM_METHOD_QUALITY)
			{
				config.g_usage = config.rc_end_usage = VPX_CQ;
				config.rc_target_bitrate = 1000000; // what they do in libvpxenc.c in FFmpeg
			}
			else
			{
				if(method == WEBM_METHOD_VBR)
				{
					config.g_usage = config.rc_end_usage = VPX_VBR;
					
					if(vbr_pass)
					{
						config.g_pass = VPX_RC_FIRST_PASS;
					}
					else
					{
						config.g_pass = VPX_RC_LAST_PASS;
						
						config.rc_twopass_stats_in.buf = vbr_buffer;
						config.rc_twopass_stats_in.sz = vbr_buffer_size;
					}
				}
				else
				{
					config.g_usage = config.rc_end_usage = VPX_CBR;
					config.g_pass = VPX_RC_ONE_PASS;
				}
				
				config.rc_target_bitrate = bitrateP.value.intValue;
			}
			
			
			config.g_threads = g_num_cpus;
			
			config.g_timebase.num = fps.denominator;
			config.g_timebase.den = fps.numerator;
			
			ConfigureEncoderPre(config, customArgs);
		
		
			codec_err = vpx_codec_enc_init(&encoder, iface, &config, 0);
			
			
			if(codec_err == VPX_CODEC_OK)
			{
				if(method == WEBM_METHOD_QUALITY)
				{
					// our slider goes 0..100, quality goes 0..63, and it's reversed
					int qual = ((float)videoQualityP.value.intValue * 63.f / 100.f) + 0.5f; // old formula for the header that says it's 0..63
					int quan = (((float)(100 - videoQualityP.value.intValue) / 100.f) * (config.rc_max_quantizer - config.rc_min_quantizer)) + config.rc_min_quantizer + 0.5f;
				
					vpx_codec_err_t config_err = vpx_codec_control(&encoder, VP8E_SET_CQ_LEVEL, quan);
					
					assert(config_err == VPX_CODEC_OK);
				}
				
				ConfigureEncoderPost(&encoder, customArgs);
			}
		}
		
	
	#define OV_OK 0
	
		int v_err = OV_OK;
	
		vorbis_info vi;
		vorbis_comment vc;
		vorbis_dsp_state vd;
		vorbis_block vb;
		ogg_packet op;
		
		bool packet_waiting = false;
		op.granulepos = 0;
										
		size_t private_size = 0;
		void *private_data = NULL;
		
		csSDK_int32 maxBlip = 100;
		
		if(exportInfoP->exportAudio && !vbr_pass)
		{
			vorbis_info_init(&vi);
			
			if(audioMethodP.value.intValue == OGG_BITRATE)
			{
				v_err = vorbis_encode_init(&vi,
											audioChannels,
											sampleRateP.value.floatValue,
											-1,
											audioBitrateP.value.intValue * 1000,
											-1);
			}
			else
			{
				v_err = vorbis_encode_init_vbr(&vi,
												audioChannels,
												sampleRateP.value.floatValue,
												audioQualityP.value.floatValue);
			}
			
			if(v_err == OV_OK)
			{
				vorbis_comment_init(&vc);
				vorbis_analysis_init(&vd, &vi);
				vorbis_block_init(&vd, &vb);
				
				
				ogg_packet header;
				ogg_packet header_comm;
				ogg_packet header_code;
				
				vorbis_analysis_headerout(&vd, &vc, &header, &header_comm, &header_code);
				
				private_data = MakePrivateData(header, header_comm, header_code, private_size);
			}
			
			mySettings->sequenceAudioSuite->GetMaxBlip(audioRenderID, frameRateP.value.timeValue, &maxBlip);
		}
		
		
		if(codec_err == VPX_CODEC_OK && v_err == OV_OK)
		{
			PrMkvWriter writer(mySettings->exportFileSuite, exportInfoP->fileObject);

			mkvmuxer::Segment muxer_segment;
			
			muxer_segment.Init(&writer);
			muxer_segment.set_mode(mkvmuxer::Segment::kFile);
			
			
			mkvmuxer::SegmentInfo* const info = muxer_segment.GetSegmentInfo();
			
			info->set_writing_app("fnord WebM for Premiere");
			
			// I'd say think about lowering this to get better precision,
			// but I get some messed up stuff when I do that.  Maybe a bug in the muxer?
			long long timeCodeScale = 1000000UL;
			
			info->set_timecode_scale(timeCodeScale);
			
			
			uint64 vid_track = 0;
			
			if(exportInfoP->exportVideo)
			{
				vid_track = muxer_segment.AddVideoTrack(renderParms.inWidth, renderParms.inHeight, 1);
				
				mkvmuxer::VideoTrack* const video = static_cast<mkvmuxer::VideoTrack *>(muxer_segment.GetTrackByNumber(vid_track));
				
				video->set_frame_rate((double)fps.numerator / (double)fps.denominator);

				video->set_codec_id(codecP.value.intValue == WEBM_CODEC_VP9 ? "V_VP9" :
										mkvmuxer::Tracks::kVp8CodecId);
				
				muxer_segment.CuesTrack(vid_track);
			}
			
			
			uint64 audio_track = 0;
			
			if(exportInfoP->exportAudio)
			{
				audio_track = muxer_segment.AddAudioTrack(sampleRateP.value.floatValue, audioChannels, 2);
				
				mkvmuxer::AudioTrack* const audio = static_cast<mkvmuxer::AudioTrack *>(muxer_segment.GetTrackByNumber(audio_track));
				
				audio->set_codec_id(mkvmuxer::Tracks::kVorbisCodecId);
				
				if(private_data)
				{
					bool copied = audio->SetCodecPrivate((const uint8 *)private_data, private_size);
					
					assert(copied);
					
					free(private_data);
				}

				if(!exportInfoP->exportVideo)
					muxer_segment.CuesTrack(audio_track);
			}
			
			PrAudioSample currentAudioSample = 0;
			const PrAudioSample endAudioSample = exportInfoP->endTime * (PrAudioSample)sampleRateP.value.floatValue / ticksPerSecond;
			
		
			PrTime videoTime = exportInfoP->startTime;
			
			while(videoTime < exportInfoP->endTime && result == malNoError)
			{
				const PrTime fileTime = videoTime - exportInfoP->startTime;
				const PrTime nextFileTime = fileTime + frameRateP.value.timeValue;
				
				// this is for the encoder, which does its own math based on config.g_timebase
				// let's do the math
				// time = timestamp * timebase :: time = videoTime / ticksPerSecond : timebase = 1 / fps
				// timestamp = time / timebase
				// timestamp = (videoTime / ticksPerSecond) * (fps.num / fps.den)
				const vpx_codec_pts_t encoder_timeStamp = fileTime * fps.numerator / (ticksPerSecond * fps.denominator);
				const vpx_codec_pts_t encoder_nextTimeStamp = (nextFileTime - exportInfoP->startTime) * fps.numerator / (ticksPerSecond * fps.denominator);
				const unsigned long encoder_duration = encoder_nextTimeStamp - encoder_timeStamp;
				
				
				// This is the key step, where we quantize our time based on the timeCode
				// to match how the frames are actually stored by the muxer.  If you want more precision,
				// lower timeCodeScale.  Time (in nanoseconds) = TimeCode * TimeCodeScale.
				const long long timeCode = ((fileTime * (1000000000UL / timeCodeScale)) + (ticksPerSecond / 2)) / ticksPerSecond;
				const long long nextTimeCode = ((nextFileTime * (1000000000UL / timeCodeScale)) + (ticksPerSecond / 2)) / ticksPerSecond;
				
				const uint64_t timeStamp = timeCode * timeCodeScale;
				const uint64_t nextTimeStamp = nextTimeCode * timeCodeScale;
			
				
				if(exportInfoP->exportAudio && !vbr_pass)
				{
					const PrAudioSample nextBlockAudoSample = nextTimeStamp * (PrAudioSample)sampleRateP.value.floatValue / 1000000000UL;
					
					while(op.granulepos < nextBlockAudoSample && currentAudioSample < endAudioSample && result == malNoError)
					{
						if(packet_waiting && op.packet != NULL && op.bytes > 0)
						{
							bool added = muxer_segment.AddFrame(op.packet, op.bytes,
																audio_track, timeStamp, 0);
																	
							if(added)
							{
								packet_waiting = false;
								
								// might also be extra blocks hanging around
								while(vorbis_analysis_blockout(&vd, &vb) == 1)
								{
									vorbis_analysis(&vb, NULL);
									vorbis_bitrate_addblock(&vb);
									
									while( vorbis_bitrate_flushpacket(&vd, &op) )
									{
										assert(packet_waiting == false);
									
										if(op.granulepos < nextBlockAudoSample)
										{
											bool added = muxer_segment.AddFrame(op.packet, op.bytes,
																				audio_track, timeStamp, 0);
																					
											if(!added)
												result = exportReturn_InternalError;
										}
										else
											packet_waiting = true;
									}
									
									if(packet_waiting)
										break;
								}
							}
							else
								result = exportReturn_InternalError;
						}
						
						
						if(!packet_waiting)
						{
							int samples = maxBlip;
							
							if(samples > (endAudioSample - currentAudioSample))
								samples = (endAudioSample - currentAudioSample);
							
							float **buffer = vorbis_analysis_buffer(&vd, samples);
							
							
							result = audioSuite->GetAudio(audioRenderID, samples, buffer, false);
							
							currentAudioSample += samples;
							
							
							if(result == malNoError)
							{
								vorbis_analysis_wrote(&vd, samples);
						
								while(vorbis_analysis_blockout(&vd, &vb) == 1)
								{
									vorbis_analysis(&vb, NULL);
									vorbis_bitrate_addblock(&vb);

									assert(packet_waiting == false);
									
									while( vorbis_bitrate_flushpacket(&vd, &op) )
									{
										assert(packet_waiting == false);
									
										if(op.granulepos < nextBlockAudoSample)
										{
											bool added = muxer_segment.AddFrame(op.packet, op.bytes,
																				audio_track, timeStamp, 0);
																					
											if(!added)
												result = exportReturn_InternalError;
										}
										else
											packet_waiting = true;
									}
									
									if(packet_waiting)
										break;
								}
							}
						}
					}
				
					// save the rest of the audio if this is the last frame
					if(result == malNoError &&
						(videoTime >= (exportInfoP->endTime - frameRateP.value.timeValue)))
					{
						vorbis_analysis_wrote(&vd, NULL); // means there will be no more data
				
						while(vorbis_analysis_blockout(&vd, &vb) == 1)
						{
							vorbis_analysis(&vb, NULL);
							vorbis_bitrate_addblock(&vb);

							while( vorbis_bitrate_flushpacket(&vd, &op) )
							{
								bool added = muxer_segment.AddFrame(op.packet, op.bytes,
																	audio_track, timeStamp, 0);
																		
								if(!added)
									result = exportReturn_InternalError;
							}
						}
					}
				}
				
				
				if(exportInfoP->exportVideo)
				{
					unsigned long deadline = vidEncodingP.value.intValue == WEBM_ENCODING_REALTIME ? VPX_DL_REALTIME :
												vidEncodingP.value.intValue == WEBM_ENCODING_BEST ? VPX_DL_BEST_QUALITY :
												VPX_DL_GOOD_QUALITY;
												
							
					SequenceRender_GetFrameReturnRec renderResult;
					
					result = renderSuite->RenderVideoFrame(videoRenderID,
															videoTime,
															&renderParms,
															kRenderCacheType_None,
															&renderResult);
					
					if(result == suiteError_NoError)
					{
						PrPixelFormat pixFormat;
						prRect bounds;
						csSDK_uint32 parN, parD;
						
						pixSuite->GetPixelFormat(renderResult.outFrame, &pixFormat);
						pixSuite->GetBounds(renderResult.outFrame, &bounds);
						pixSuite->GetPixelAspectRatio(renderResult.outFrame, &parN, &parD);
						
						const int width = bounds.right - bounds.left;
						const int height = bounds.bottom - bounds.top;
						
						
						// libvpx can only take PX_IMG_FMT_YV12, VPX_IMG_FMT_I420, VPX_IMG_FMT_VPXI420, VPX_IMG_FMT_VPXYV12
						// (the latter two are in "vpx color space"?)
						// see validate_img() in vp8_cx_iface.c
						// TODO: VP9 can take VPX_IMG_FMT_I422 and VPX_IMG_FMT_I444
						// although you probably want to switch to YV12 and yuvconfig2image()
						// Enable CONFIG_ALPHA to use alpha
								
						vpx_image_t img_data;
						vpx_image_t *img = vpx_img_alloc(&img_data, VPX_IMG_FMT_I420, width, height, 32);
						
						if(img)
						{
							if(pixFormat == PrPixelFormat_YUV_420_MPEG2_FRAME_PICTURE_PLANAR_8u_709)
							{
								char *Y_PixelAddress, *U_PixelAddress, *V_PixelAddress;
								csSDK_uint32 Y_RowBytes, U_RowBytes, V_RowBytes;
								
								pix2Suite->GetYUV420PlanarBuffers(renderResult.outFrame, PrPPixBufferAccess_ReadOnly,
																	&Y_PixelAddress, &Y_RowBytes,
																	&U_PixelAddress, &U_RowBytes,
																	&V_PixelAddress, &V_RowBytes);
								
								for(int y = 0; y < img->d_h; y++)
								{
									unsigned char *imgY = img->planes[VPX_PLANE_Y] + (img->stride[VPX_PLANE_Y] * y);
									
									unsigned char *prY = (unsigned char *)Y_PixelAddress + (Y_RowBytes * y);
									
									memcpy(imgY, prY, img->d_w * sizeof(unsigned char));
								}
								
								for(int y = 0; y < img->d_h / 2; y++)
								{
									unsigned char *imgU = img->planes[VPX_PLANE_U] + (img->stride[VPX_PLANE_U] * y);
									unsigned char *imgV = img->planes[VPX_PLANE_V] + (img->stride[VPX_PLANE_V] * y);
									
									unsigned char *prU = (unsigned char *)U_PixelAddress + (U_RowBytes * y);
									unsigned char *prV = (unsigned char *)V_PixelAddress + (V_RowBytes * y);
									
									memcpy(imgU, prU, (img->d_w / 2) * sizeof(unsigned char));
									memcpy(imgV, prV, (img->d_w / 2) * sizeof(unsigned char));
								}
							}
							else if(pixFormat == PrPixelFormat_BGRA_4444_16u)
							{
								// since we're doing an RGB to YUV conversion, it wouldn't hurt to have some extra bits
								char *frameBufferP = NULL;
								csSDK_int32 rowbytes = 0;
								
								pixSuite->GetPixels(renderResult.outFrame, PrPPixBufferAccess_ReadOnly, &frameBufferP);
								pixSuite->GetRowBytes(renderResult.outFrame, &rowbytes);
								
								
								for(int y = 0; y < img->d_h; y++)
								{
									unsigned char *imgY = img->planes[VPX_PLANE_Y] + (img->stride[VPX_PLANE_Y] * y);
									unsigned char *imgU = img->planes[VPX_PLANE_U] + (img->stride[VPX_PLANE_U] * (y / 2));
									unsigned char *imgV = img->planes[VPX_PLANE_V] + (img->stride[VPX_PLANE_V] * (y / 2));
									
									unsigned short *prBGRA = (unsigned short *)((unsigned char *)frameBufferP + (rowbytes * (img->d_h - 1 - y)));
									
									unsigned short *prB = prBGRA + 0;
									unsigned short *prG = prBGRA + 1;
									unsigned short *prR = prBGRA + 2;
									unsigned short *prA = prBGRA + 3;
									
									for(int x=0; x < img->d_w; x++)
									{
										*imgY++ = Convert16to8( ((257 * (int)*prR) + (504 * (int)*prG) + ( 98 * (int)*prB) + 2056500) / 1000 );
										
										if( (y % 2 == 0) && (x % 2 == 0) )
										{
											*imgV++ = Convert16to8( ((439 * (int)*prR) - (368 * (int)*prG) - ( 71 * (int)*prB) + 16449500) / 1000 );
											*imgU++ = Convert16to8( (-(148 * (int)*prR) - (291 * (int)*prG) + (439 * (int)*prB) + 16449500) / 1000 );
										}
										
										prR += 4;
										prG += 4;
										prB += 4;
										prA += 4;
									}
								}
							}
							else if(pixFormat == PrPixelFormat_BGRA_4444_8u)
							{
								// so here's our dumb RGB to YUV conversion
							
								char *frameBufferP = NULL;
								csSDK_int32 rowbytes = 0;
								
								pixSuite->GetPixels(renderResult.outFrame, PrPPixBufferAccess_ReadOnly, &frameBufferP);
								pixSuite->GetRowBytes(renderResult.outFrame, &rowbytes);
								
								
								for(int y = 0; y < img->d_h; y++)
								{
									// using the conversion found here: http://www.fourcc.org/fccyvrgb.php
									
									unsigned char *imgY = img->planes[VPX_PLANE_Y] + (img->stride[VPX_PLANE_Y] * y);
									unsigned char *imgU = img->planes[VPX_PLANE_U] + (img->stride[VPX_PLANE_U] * (y / 2));
									unsigned char *imgV = img->planes[VPX_PLANE_V] + (img->stride[VPX_PLANE_V] * (y / 2));
									
									// the rows in this kind of Premiere buffer are flipped, FYI (or is it flopped?)
									unsigned char *prBGRA = (unsigned char *)frameBufferP + (rowbytes * (img->d_h - 1 - y));
									
									unsigned char *prB = prBGRA + 0;
									unsigned char *prG = prBGRA + 1;
									unsigned char *prR = prBGRA + 2;
									unsigned char *prA = prBGRA + 3;
									
									for(int x=0; x < img->d_w; x++)
									{
										// like the clever integer (fixed point) math?
										*imgY++ = ((257 * (int)*prR) + (504 * (int)*prG) + ( 98 * (int)*prB) + 16500) / 1000;
										
										if( (y % 2 == 0) && (x % 2 == 0) )
										{
											*imgV++ = ((439 * (int)*prR) - (368 * (int)*prG) - ( 71 * (int)*prB) + 128500) / 1000;
											*imgU++ = (-(148 * (int)*prR) - (291 * (int)*prG) + (439 * (int)*prB) + 128500) / 1000;
										}
										
										prR += 4;
										prG += 4;
										prB += 4;
										prA += 4;
									}
								}
							}
							
							
							vpx_codec_err_t encode_err = vpx_codec_encode(&encoder, img, encoder_timeStamp, encoder_duration, 0, deadline);
							
							if(encode_err == VPX_CODEC_OK)
							{
								const vpx_codec_cx_pkt_t *pkt = NULL;
								vpx_codec_iter_t iter = NULL;
								 
								while( (pkt = vpx_codec_get_cx_data(&encoder, &iter)) )
								{
									if(pkt->kind == VPX_CODEC_CX_FRAME_PKT)
									{
										assert(!vbr_pass);
									
										bool added = muxer_segment.AddFrame((const uint8 *)pkt->data.frame.buf, pkt->data.frame.sz,
																			vid_track, timeStamp,
																			(pkt->data.frame.flags & VPX_FRAME_IS_KEY));
																			
										if(!added)
											result = exportReturn_InternalError;
									}
									else if(pkt->kind == VPX_CODEC_STATS_PKT)
									{
										assert(vbr_pass);
										
										if(vbr_buffer_size == 0)
											vbr_buffer = memorySuite->NewPtr(pkt->data.frame.sz);
										else
											memorySuite->SetPtrSize(&vbr_buffer, vbr_buffer_size + pkt->data.frame.sz);
										
										memcpy(&vbr_buffer[vbr_buffer_size], pkt->data.frame.buf, pkt->data.frame.sz);
										
										vbr_buffer_size += pkt->data.frame.sz;
									}
								}
							}
							else
								result = exportReturn_InternalError;
							
							vpx_img_free(img);
						}
						else
							result = exportReturn_ErrMemory;
						
						pixSuite->Dispose(renderResult.outFrame);
					}
					else
						assert(false); // error retreiving frame?
					
					
					// squeeze last bits from encoder
					if(result == malNoError &&
						(videoTime >= (exportInfoP->endTime - frameRateP.value.timeValue)))
					{
						const vpx_codec_cx_pkt_t *pkt = NULL;
						vpx_codec_iter_t iter = NULL;
						
						do{
							vpx_codec_encode(&encoder, NULL, encoder_timeStamp, encoder_duration, 0, deadline);
							
							pkt = vpx_codec_get_cx_data(&encoder, &iter);
							
							if(pkt != NULL)
							{
								if(pkt->kind == VPX_CODEC_CX_FRAME_PKT)
								{
									assert(!vbr_pass);
									
									bool added = muxer_segment.AddFrame((const uint8 *)pkt->data.frame.buf, pkt->data.frame.sz,
																		vid_track, timeStamp,
																		(pkt->data.frame.flags & VPX_FRAME_IS_KEY));
									assert(added);
								}
								else if(pkt->kind == VPX_CODEC_STATS_PKT)
								{
									assert(vbr_pass);
									
									if(vbr_buffer_size == 0)
										vbr_buffer = memorySuite->NewPtr(pkt->data.frame.sz);
									else
										memorySuite->SetPtrSize(&vbr_buffer, vbr_buffer_size + pkt->data.frame.sz);
									
									memcpy(&vbr_buffer[vbr_buffer_size], pkt->data.frame.buf, pkt->data.frame.sz);
									
									vbr_buffer_size += pkt->data.frame.sz;
								}
							}
							
						}while(pkt != NULL);
					}
				}
				
				
				if(result == malNoError)
				{
					float progress = (double)(videoTime - exportInfoP->startTime) / (double)(exportInfoP->endTime - exportInfoP->startTime);
					
					if(passes == 2)
						progress = (progress / 2.f) + (0.5f * pass);

					result = mySettings->exportProgressSuite->UpdateProgressPercent(exID, progress);
					
					if(result == suiteError_ExporterSuspended)
					{
						result = mySettings->exportProgressSuite->WaitForResume(exID);
					}
				}
				
				
				videoTime += frameRateP.value.timeValue;
			}
			
			
			bool final = muxer_segment.Finalize();
			
			if(!final && !vbr_pass)
				result = exportReturn_InternalError;
		}
		else
			result = exportReturn_InternalError;
		


		if(exportInfoP->exportVideo)
		{
			vpx_codec_err_t destroy_err = vpx_codec_destroy(&encoder);
			assert(destroy_err == VPX_CODEC_OK);
		}
			
		if(exportInfoP->exportAudio && !vbr_pass)
		{
			vorbis_block_clear(&vb);
			vorbis_dsp_clear(&vd);
			vorbis_comment_clear(&vc);
			vorbis_info_clear(&vi);
		}
	}
	
	}catch(...) { result = exportReturn_InternalError; }
	
	
	if(vbr_buffer != NULL)
		memorySuite->PrDisposePtr(vbr_buffer);
	
	
	if(exportInfoP->exportVideo)
		renderSuite->ReleaseVideoRenderer(exID, videoRenderID);

	if(exportInfoP->exportAudio)
		audioSuite->ReleaseAudioRenderer(exID, audioRenderID);
	

	return result;
}




DllExport PREMPLUGENTRY xSDKExport (
	csSDK_int32		selector, 
	exportStdParms	*stdParmsP, 
	void			*param1, 
	void			*param2)
{
	prMALError result = exportReturn_Unsupported;
	
	switch (selector)
	{
		case exSelStartup:
			result = exSDKStartup(	stdParmsP, 
									reinterpret_cast<exExporterInfoRec*>(param1));
			break;

		case exSelBeginInstance:
			result = exSDKBeginInstance(stdParmsP,
										reinterpret_cast<exExporterInstanceRec*>(param1));
			break;

		case exSelEndInstance:
			result = exSDKEndInstance(	stdParmsP,
										reinterpret_cast<exExporterInstanceRec*>(param1));
			break;

		case exSelGenerateDefaultParams:
			result = exSDKGenerateDefaultParams(stdParmsP,
												reinterpret_cast<exGenerateDefaultParamRec*>(param1));
			break;

		case exSelPostProcessParams:
			result = exSDKPostProcessParams(stdParmsP,
											reinterpret_cast<exPostProcessParamsRec*>(param1));
			break;

		case exSelGetParamSummary:
			result = exSDKGetParamSummary(	stdParmsP,
											reinterpret_cast<exParamSummaryRec*>(param1));
			break;

		case exSelQueryOutputSettings:
			result = exSDKQueryOutputSettings(	stdParmsP,
												reinterpret_cast<exQueryOutputSettingsRec*>(param1));
			break;

		case exSelQueryExportFileExtension:
			result = exSDKFileExtension(stdParmsP,
										reinterpret_cast<exQueryExportFileExtensionRec*>(param1));
			break;

		case exSelValidateParamChanged:
			result = exSDKValidateParamChanged(	stdParmsP,
												reinterpret_cast<exParamChangedRec*>(param1));
			break;

		case exSelValidateOutputSettings:
			result = malNoError;
			break;

		case exSelExport:
			result = exSDKExport(	stdParmsP,
									reinterpret_cast<exDoExportRec*>(param1));
			break;
	}
	
	return result;
}

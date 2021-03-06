﻿#include <iostream>
#include <fstream>

#include "../src/gutil.h"
#include "../src/gdemux.h"
#include "../src/gdec.h"
#include "../src/genc.h"
#include "../src/gmux.h"
#include "../src/gsws.h"
#include "../src/gswr.h"

#define     G_ERROR_SUCCEED          0      //succeed
#define     G_ERROR_INVALIDPARAM    -1      //invalid param
#define     G_ERROR_INTERNAL        -2      //internal call error
#define     G_ERROR_NOBUF           -3      //new buffer failed

#ifdef _WIN32
#include <windows.h>
#endif

namespace gconvert
{
#ifdef _WIN32
	static int multi2uni(const std::string& multi, std::wstring& uni, UINT code)
	{
		auto len = MultiByteToWideChar(code, 0, multi.c_str(), -1, nullptr, 0);
		if (len <= 0)
		{
			std::cerr << __FILE__ << " : " << __LINE__ << " : " << GetLastError() << std::endl;
			return G_ERROR_INVALIDPARAM;
		}
		WCHAR* buf = new WCHAR[len];
		if (buf == nullptr)
		{
			std::cerr << __FILE__ << " : " << __LINE__ << " : " << "can not new buf, size : " << len << std::endl;
			return G_ERROR_NOBUF;
		}
		len = MultiByteToWideChar(code, 0, multi.c_str(), -1, buf, len);
		uni.assign(buf);
		delete[]buf;
		buf = nullptr;
		return len;
	}

	static int uni2multi(const std::wstring& uni, std::string& multi, UINT code)
	{
		auto len = WideCharToMultiByte(code, 0, uni.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (len <= 0)
		{
			std::cerr << __FILE__ << " : " << __LINE__ << " : " << GetLastError() << std::endl;
			return G_ERROR_INVALIDPARAM;
		}
		CHAR* buf = new CHAR[len];
		if (buf == nullptr)
		{
			std::cerr << __FILE__ << " : " << __LINE__ << " : " << "can not new buf, size : " << len << std::endl;
			return G_ERROR_NOBUF;
		}
		len = WideCharToMultiByte(code, 0, uni.c_str(), -1, buf, len, nullptr, nullptr);
		multi.assign(buf);
		delete[]buf;
		buf = nullptr;
		return len;
	}
#endif

	// ANSI->Unicode
	int ansi2uni(const std::string& ansi, std::wstring& uni)
	{
#ifdef _WIN32
		return multi2uni(ansi, uni, CP_ACP);
#endif
		return G_ERROR_SUCCEED;
	}

	// Unicode->ANSI
	int uni2ansi(const std::wstring& uni, std::string& ansi)
	{
#ifdef _WIN32
		return uni2multi(uni, ansi, CP_ACP);
#endif
		return G_ERROR_SUCCEED;
	}

	// UTF8->Unicode
	int utf82uni(const std::string& utf8, std::wstring& uni)
	{
#ifdef _WIN32
		return multi2uni(utf8, uni, CP_UTF8);
#endif
		return G_ERROR_SUCCEED;
	}

	// Unicode->UTF8
	int uni2utf8(const std::wstring& uni, std::string& utf8)
	{
#ifdef _WIN32
		return uni2multi(uni, utf8, CP_UTF8);
#endif
		return G_ERROR_SUCCEED;
	}

	// ANSI->UTF8
	int ansi2utf8(const std::string& ansi, std::string& utf8)
	{
		std::wstring uni;
		auto len = ansi2uni(ansi, uni);
		if (len <= 0)
		{
			return G_ERROR_INTERNAL;
		}
		return uni2utf8(uni, utf8);
	}

	// UTF8->ANSI
	int utf82ansi(const std::string& utf8, std::string& ansi)
	{
		std::wstring uni;
		auto len = utf82uni(utf8, uni);
		if (len <= 0)
		{
			return G_ERROR_INTERNAL;
		}
		return uni2ansi(uni, ansi);
	}
} // namespace gconvert

int readpacket(void* opaque, uint8_t* buf, int buf_size)
{
	static std::ifstream f("gx.mkv", std::ios::binary);
	f.read(reinterpret_cast<char*>(buf), buf_size);
	buf_size = static_cast<int>(f.gcount());
	return buf_size <= 0 ? EOF : buf_size;
}

int test_demux(const char* in)
{
	gff::gdemux demux;
	auto ret = demux.open(nullptr, nullptr, {}, readpacket, nullptr);
	CHECKFFRET(ret);
	const AVCodecParameters* par = nullptr;
	AVRational timebase;
	int64_t duration = 0;
	ret = demux.get_duration(duration, { 1, 1000 });
	CHECKFFRET(ret);
	std::cout << "duration : " << duration << " s" << std::endl;
	auto packet = gff::GetPacket();
	while (demux.readpacket(packet) == 0)
	{
		demux.get_stream_par(packet->stream_index, par, timebase);
		std::cout << "pts : " << av_rescale_q(packet->pts, timebase, { 1,1 }) << " " << packet->stream_index << std::endl;
		if (av_rescale_q(packet->pts, timebase, { 1,1 }) >= 10)
		{
			static int times = 0;
			if (times++ < 10)
				demux.seek_frame(packet->stream_index, av_rescale_q(2, { 1,1 }, timebase));
		}
	}
	ret = demux.cleanup();
	CHECKFFRET(ret);

	return 0;
}

int test_dec(const char* in)
{
	gff::gdemux demux;
	auto ret = demux.open(in, nullptr, {}, readpacket, nullptr, 10240);

	auto packet = gff::GetPacket();
	auto frame = gff::GetFrame();
	auto frame2 = gff::GetFrame();

	std::vector<unsigned int> videovec, audiovec;
	ret = demux.get_steam_index(videovec, audiovec);
	CHECKFFRET(ret);
	const AVCodecParameters* vpar = nullptr;
	AVRational vtimebase, atimebase;
	const AVCodecParameters* apar = nullptr;
	ret = demux.get_stream_par(videovec.at(0), vpar, vtimebase);
	CHECKFFRET(ret);
	ret = demux.get_stream_par(audiovec.at(0), apar, atimebase);
	CHECKFFRET(ret);

	gff::gdec vdec;
	ret = vdec.copy_param(vpar, AV_HWDEVICE_TYPE_CUDA);
	CHECKFFRET(ret);
	gff::gdec adec;
	ret = adec.copy_param(apar);
	CHECKFFRET(ret);

	while (demux.readpacket(packet) == 0)
	{
		if (packet->stream_index == videovec.at(0))
		{
			if (vdec.decode(packet, frame) >= 0)
			{
				do
				{
					std::cout << "format : " << frame->format << " pts : " << av_rescale_q(frame->pts, vtimebase, { 1,1 }) << " " << packet->stream_index << std::endl;
					if (frame->format == AV_PIX_FMT_YUV420P)
					{
						static std::ofstream f("out.yuv", std::ios::binary | std::ios::trunc);
						f.write(reinterpret_cast<const char*>(frame->data[0]), static_cast<int64_t>(frame->linesize[0]) * frame->height);
						f.write(reinterpret_cast<const char*>(frame->data[1]), static_cast<int64_t>(frame->linesize[1]) * frame->height / 2);
						f.write(reinterpret_cast<const char*>(frame->data[2]), static_cast<int64_t>(frame->linesize[2]) * frame->height / 2);
					}
					else if (frame->format == AV_PIX_FMT_CUDA)
					{
						ret = gff::hwframe_to_frame(frame, frame2);
						CHECKFFRET(ret);
						if (frame2->format == AV_PIX_FMT_NV12)
						{
							static std::ofstream f("out.nv12", std::ios::binary | std::ios::trunc);
							f.write(reinterpret_cast<const char*>(frame2->data[0]), static_cast<int64_t>(frame2->linesize[0]) * frame2->height);
							f.write(reinterpret_cast<const char*>(frame2->data[1]), static_cast<int64_t>(frame2->linesize[1]) * frame2->height / 2);
						}
					}
				} while (vdec.decode(nullptr, frame) >= 0);
			}
		}
		else if (packet->stream_index == audiovec.at(0))
		{
			if (adec.decode(packet, frame) >= 0)
			{
				do
				{
					std::cout << "pts : " << av_rescale_q(frame->pts, atimebase, { 1,1 }) << " " << packet->stream_index << std::endl;
					static std::ofstream f("out.pcm", std::ios::binary | std::ios::trunc);
					auto size = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));
					for (int i = 0; i < frame->nb_samples; ++i)
					{
						for (int j = 0; j < frame->channels; ++j)
						{
							f.write(reinterpret_cast<const char*>(frame->data[j] + size * i), size);
						}
					}
				} while (adec.decode(nullptr, frame) >= 0);
			}
		}
	}

	vdec.cleanup();
	adec.cleanup();
	demux.cleanup();

	return 0;
}

int test_dec_h264(const char* in)
{
	gff::gdec vdec;
	AVCodecParameters par = { AVMEDIA_TYPE_VIDEO, AV_CODEC_ID_H264, };
	auto ret = vdec.copy_param(&par);
	CHECKFFRET(ret);

	std::ifstream f(in, std::ios::binary);
	char buf[1024] = { 0 };
	std::ofstream out("out.yuv", std::ios::binary | std::ios::trunc);
	while (f.read(buf, sizeof(buf)))
	{
		uint32_t buflen = static_cast<uint32_t>(f.gcount());
		int len = 0;
		uint32_t uselen = 0;
		do {
			auto frame = gff::GetFrame();
			ret = vdec.decode(buf + uselen, buflen - uselen, frame, len);
			CHECKFFRET(ret);
			if (frame->data[0] != nullptr)
			{
				if (frame->format == AV_PIX_FMT_YUV420P)
				{
					out.write(reinterpret_cast<const char*>(frame->data[0]), static_cast<int64_t>(frame->linesize[0]) * frame->height);
					out.write(reinterpret_cast<const char*>(frame->data[1]), static_cast<int64_t>(frame->linesize[1]) * frame->height / 2);
					out.write(reinterpret_cast<const char*>(frame->data[2]), static_cast<int64_t>(frame->linesize[2]) * frame->height / 2);
				}
			}

			uselen += len;
			while (ret == AVERROR(EAGAIN))
			{
				auto frame = gff::GetFrame();
				ret = vdec.decode(nullptr, 0, frame, len);
				CHECKFFRET(ret);
				if (frame->data[0] != nullptr)
				{
					if (frame->format == AV_PIX_FMT_YUV420P)
					{
						out.write(reinterpret_cast<const char*>(frame->data[0]), static_cast<int64_t>(frame->linesize[0]) * frame->height);
						out.write(reinterpret_cast<const char*>(frame->data[1]), static_cast<int64_t>(frame->linesize[1]) * frame->height / 2);
						out.write(reinterpret_cast<const char*>(frame->data[2]), static_cast<int64_t>(frame->linesize[2]) * frame->height / 2);
					}
				}
			}
		} while (buflen > uselen);
	}

	vdec.cleanup();
	return 0;
}

int test_enc_video(const char* in)
{
	const int width = 640;
	const int height = 480;
	std::ifstream yuv(in, std::ios::binary);

	gff::genc enc;
	auto ret = enc.set_video_param("libx264", 10000000, width, height, { 1,24 }, { 24,1 }, 5, 0, AV_PIX_FMT_YUV420P);
	CHECKFFRET(ret);
	const AVCodecContext* codectx = nullptr;
	ret = enc.get_codectx(codectx);
	CHECKFFRET(ret);

	std::ofstream out("out.h264", std::ios::binary | std::ios::trunc);

	while (!yuv.eof())
	{
		auto packet = gff::GetPacket();
		auto frame = gff::GetFrame();
		ret = gff::GetFrameBuf(frame, width, height, AV_PIX_FMT_YUV420P, 1);
		CHECKFFRET(ret);
		ret = gff::frame_make_writable(frame);
		CHECKFFRET(ret);

		yuv.read(reinterpret_cast<char*>(frame->data[0]), static_cast<std::streamsize>(frame->linesize[0]) * frame->height);
		yuv.read(reinterpret_cast<char*>(frame->data[1]), static_cast<std::streamsize>(frame->linesize[1]) * frame->height / 2);
		yuv.read(reinterpret_cast<char*>(frame->data[2]), static_cast<std::streamsize>(frame->linesize[2]) * frame->height / 2);

		static int i = 0;
		frame->pts = i++;
		if (enc.encode_push_frame(frame) == 0)
		{
			while (enc.encode_get_packet(packet) == 0)
			{
				std::cout << "pts : " << av_rescale_q(packet->pts, codectx->time_base, { 1,1 }) << std::endl;
				out.write(reinterpret_cast<char*>(packet->data), packet->size);
			}
		}
	}

	enc.encode_push_frame(nullptr);
	auto packet = gff::GetPacket();
	while (enc.encode_get_packet(packet) == 0)
	{
		std::cout << "pts : " << av_rescale_q(packet->pts, codectx->time_base, { 1,1 }) << std::endl;
		out.write(reinterpret_cast<char*>(packet->data), packet->size);
	}
	enc.cleanup();

	return 0;
}

int test_enc_audio(const char* in)
{
	const int bufsize = 10240;
	std::ifstream pcm("out.pcm", std::ios::binary);
	std::ofstream out("out.mp3", std::ios::binary | std::ios::trunc);
	char buf[bufsize] = { 0 };
	gff::genc enc;
	int framesize = 0;

	auto ret = enc.set_audio_param("libmp3lame", 64000, 48000, AV_CH_LAYOUT_STEREO, 2, AV_SAMPLE_FMT_FLTP, framesize);
	CHECKFFRET(ret);

	auto packet = gff::GetPacket();
	auto frame = gff::GetFrame();
	ret = gff::GetFrameBuf(frame, framesize, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, 1);
	CHECKFFRET(ret);
	auto persamplesize = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));

	const AVCodecContext* codectx = nullptr;
	ret = enc.get_codectx(codectx);
	CHECKFFRET(ret);

	while (!pcm.eof())
	{
		pcm.read(buf, static_cast<std::streamsize>(framesize) * persamplesize * av_get_channel_layout_nb_channels(frame->channel_layout));
		ret = gff::frame_make_writable(frame);
		CHECKFFRET(ret);

		for (int i = 0; i < frame->nb_samples; ++i)
		{
			memcpy_s(frame->data[0] + persamplesize * i, persamplesize, buf + persamplesize * (2 * i), persamplesize);
			memcpy_s(frame->data[1] + persamplesize * i, persamplesize, buf + persamplesize * (2 * i + 1), persamplesize);
		}
		static int i = 0;
		frame->pts = av_rescale_q(static_cast<int64_t>(frame->nb_samples) * i++, { 1, 48000 }, codectx->time_base);
		if (enc.encode_push_frame(frame) >= 0)
		{
			while (enc.encode_get_packet(packet) >= 0)
			{
				std::cout << "pts : " << av_rescale_q(packet->pts, codectx->time_base, { 1,1 }) << std::endl;
				out.write(reinterpret_cast<char*>(packet->data), packet->size);
			}
		}
	}

	enc.encode_push_frame(nullptr);
	while (enc.encode_get_packet(packet) >= 0)
	{
		std::cout << "pts : " << av_rescale_q(packet->pts, codectx->time_base, { 1,1 }) << std::endl;
	}
	enc.cleanup();

	return 0;
}

int test_mux(const char* out)
{
	std::ifstream nv12("out.nv12", std::ios::binary);
	const int width = 640;
	const int height = 480;
	AVRational ivtimebase = { 1, 24 };

	gff::genc enc;
	auto ret = enc.set_video_param("h264_qsv", 10000000, width, height, ivtimebase, { 24,1 }, 5, 0, AV_PIX_FMT_NV12);
	CHECKFFRET(ret);

	gff::gmux mux;
	ret = mux.create_output(out);
	CHECKFFRET(ret);
	const AVCodecContext* codectx = nullptr;
	ret = enc.get_codectx(codectx);
	CHECKFFRET(ret);
	int vindex = -1;
	ret = mux.create_stream(codectx, vindex);
	CHECKFFRET(ret);
	ret = mux.write_header();
	CHECKFFRET(ret);
	AVRational ovtimebase;
	ret = mux.get_timebase(vindex, ovtimebase);
	CHECKFFRET(ret);

	int i = 0;
	while (!nv12.eof())
	{
		auto packet = gff::GetPacket();
		auto frame = gff::GetFrame();
		ret = gff::GetFrameBuf(frame, width, height, AV_PIX_FMT_NV12, 1);
		CHECKFFRET(ret);
		ret = av_frame_make_writable(frame.get());
		CHECKFFRET(ret);

		nv12.read(reinterpret_cast<char*>(frame->data[0]), static_cast<std::streamsize>(frame->linesize[0]) * frame->height);
		nv12.read(reinterpret_cast<char*>(frame->data[1]), static_cast<std::streamsize>(frame->linesize[0]) * frame->height / 2);

		frame->pts = i++;
		if (enc.encode_push_frame(frame) == 0)
		{
			while (enc.encode_get_packet(packet) == 0)
			{
				packet->pts = av_rescale_q(packet->pts, ivtimebase, ovtimebase);
				packet->dts = packet->pts;
				packet->duration = 1;
				std::cout << "pts : " << av_rescale_q(packet->pts, ovtimebase, { 1,1 }) << std::endl;
				ret = mux.write_packet(packet);
				CHECKFFRET(ret);
				packet = gff::GetPacket();
			}
		}
	}

	enc.encode_push_frame(nullptr);
	auto packet = gff::GetPacket();
	while (enc.encode_get_packet(packet) == 0)
	{
		packet->pts = av_rescale_q(packet->pts, ivtimebase, ovtimebase);
		packet->dts = packet->pts;
		packet->duration = 1;
		std::cout << "pts : " << av_rescale_q(packet->pts, ovtimebase, { 1,1 }) << std::endl;
		mux.write_packet(packet);
		packet = gff::GetPacket();
	}
	mux.cleanup();
	enc.cleanup();

	return 0;
}

int test_sws(const char* in)
{
	const int width = 640;
	const int height = 480;
	std::ifstream yuv(in, std::ios::binary);
	std::ofstream nv12("out.nv12", std::ios::binary);

	auto frame = gff::GetFrame();
	auto frame2 = gff::GetFrame();
	auto ret = gff::GetFrameBuf(frame, width, height, AV_PIX_FMT_YUV420P, 1);
	CHECKFFRET(ret);
	ret = gff::GetFrameBuf(frame2, width, height, AV_PIX_FMT_NV12, 1);
	CHECKFFRET(ret);

	gff::gsws sws;
	ret = sws.create_sws(static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
		static_cast<AVPixelFormat>(frame2->format), frame2->width, frame2->height);
	CHECKFFRET(ret);

	while (!yuv.eof())
	{
		yuv.read(reinterpret_cast<char*>(frame->data[0]), width * height);
		yuv.read(reinterpret_cast<char*>(frame->data[1]), width * height / 4);
		yuv.read(reinterpret_cast<char*>(frame->data[2]), width * height / 4);

		ret = gff::frame_make_writable(frame);
		CHECKFFRET(ret);
		ret = gff::frame_make_writable(frame2);
		CHECKFFRET(ret);

		int h = sws.scale(frame->data, frame->linesize, 0, frame->height, frame2->data, frame2->linesize);
		CHECKFFRET(h);

		nv12.write(reinterpret_cast<const char*>(frame2->data[0]), static_cast<int64_t>(frame2->linesize[0]) * h);
		nv12.write(reinterpret_cast<const char*>(frame2->data[1]), static_cast<int64_t>(frame2->linesize[1]) * h / 2);
	}

	sws.cleanup();

	return 0;
}

int test_swr(const char* in)
{
	std::ifstream pcm("out.pcm", std::ios::binary);
	std::ofstream pcm2("out2.pcm", std::ios::binary);
	const int bufsize = 48000 * 2 * 4;
	char* buf = static_cast<char*>(malloc(bufsize));

	auto frame = gff::GetFrame();
	auto ret = gff::GetFrameBuf(frame, 48000, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, 1);
	CHECKFFRET(ret);
	auto size = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));
	ret = gff::frame_make_writable(frame);
	CHECKFFRET(ret);

	auto frame2 = gff::GetFrame();
	ret = gff::GetFrameBuf(frame2, 44100, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S32P, 1);
	CHECKFFRET(ret);
	ret = gff::frame_make_writable(frame2);
	CHECKFFRET(ret);

	gff::gswr swr;
	ret = swr.create_swr(frame->channel_layout, frame->nb_samples, static_cast<AVSampleFormat>(frame->format),
		frame2->channel_layout, frame2->nb_samples, static_cast<AVSampleFormat>(frame2->format));
	CHECKFFRET(ret);

	// 获取样本格式对应的每个样本大小(Byte)
	int persize = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame2->format));
	// 获取布局对应的通道数
	int channels = av_get_channel_layout_nb_channels(frame2->channel_layout);

	auto len = static_cast<std::streamsize>(frame->nb_samples) * size * channels;

	while (!pcm.eof())
	{
		pcm.read(buf, len);
		ret = gff::frame_make_writable(frame);
		CHECKFFRET(ret);

		for (int i = 0; i < frame->nb_samples; ++i)
		{
			memcpy_s(frame->data[0] + size * i, size, buf + size * (2 * i), size);
			memcpy_s(frame->data[1] + size * i, size, buf + size * (2 * i + 1), size);
		}

		ret = gff::frame_make_writable(frame2);
		CHECKFFRET(ret);
		auto swssize = swr.convert(frame2->data, frame2->linesize[0], (const uint8_t**)(frame->data), frame->nb_samples);
		// 拷贝音频数据
		for (int i = 0; i < swssize; ++i) // 每个样本
		{
			for (int j = 0; j < channels; ++j) // 每个通道
			{
				pcm2.write(reinterpret_cast<const char*>(frame2->data[j] + persize * i), persize);
			}
		}
	}

	swr.cleanup();
	free(buf);

	return 0;
}

int test_record_audio()
{
	std::string in;
	auto ret = gconvert::ansi2utf8("audio=麦克风 (Realtek High Definition Audio)", in);
	CHECKFFRET(ret);

	gff::gdemux audio;
	ret = audio.open(in.c_str(), "dshow");
	CHECKFFRET(ret);
	const AVCodecParameters* par = nullptr;
	AVRational timebase = { 0 };
	ret = audio.get_stream_par(0, par, timebase);
	CHECKFFRET(ret);

	auto samplerate = 44100;//采样率
	enum AVSampleFormat samplefmt = AV_SAMPLE_FMT_FLTP;//采样格式
	auto persize = av_get_bytes_per_sample(static_cast<enum AVSampleFormat>(samplefmt));//每个采样数据大小
	auto channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);

	gff::gswr swr;
	ret = swr.create_swr(
		par->channel_layout == 0 ? AV_CH_LAYOUT_STEREO : par->channel_layout, par->sample_rate, static_cast<enum AVSampleFormat>(par->format),
		AV_CH_LAYOUT_STEREO, samplerate, samplefmt);
	CHECKFFRET(ret);

	//重采样帧
	auto dframe = gff::GetFrame();
	ret = gff::GetFrameBuf(dframe, samplerate, AV_CH_LAYOUT_STEREO, samplefmt, 0);
	CHECKFFRET(ret);
	ret = gff::frame_make_writable(dframe);
	CHECKFFRET(ret);

	std::ofstream out("save.mp3", std::ios::binary);
	//std::ofstream sout("ssave.pcm", std::ios::binary);
	auto packet = gff::GetPacket();
	bool stop = false;

	auto framesize = 1024;
	gff::genc enc;
	ret = enc.set_audio_param("libmp3lame", 128000, samplerate, par->channel_layout == 0 ? AV_CH_LAYOUT_STEREO : par->channel_layout,
		channels, samplefmt, framesize);
	CHECKFFRET(ret);
	AVAudioFifo* fifo = av_audio_fifo_alloc(samplefmt, channels, framesize * 2);
	auto ff = gff::GetFrame();
	ret = gff::GetFrameBuf(ff, framesize, AV_CH_LAYOUT_STEREO, samplefmt, 0);
	CHECKFFRET(ret);

	std::thread th([&]() {
		while (audio.readpacket(packet) == 0 && !stop)
		{
			ret = swr.convert(dframe->data, dframe->linesize[0] / persize, 
				const_cast<const uint8_t**>(&packet->data), packet->size / persize);
			CHECKFFRET(ret);

			ret = av_audio_fifo_write(fifo, reinterpret_cast<void**>(dframe->data), ret);
			CHECKFFRET(ret);
			while (av_audio_fifo_size(fifo) >= framesize)
			{
				ret = gff::frame_make_writable(ff);
				CHECKFFRET(ret);
				ret = av_audio_fifo_read(fifo, reinterpret_cast<void**>(ff->data), framesize);
				CHECKFFRET(ret);

				ret = enc.encode_push_frame(ff);
				CHECKFFRET(ret);
				if (ret >= 0)
				{
					do{
						ret = enc.encode_get_packet(packet);
						CHECKFFRET(ret);
						if (ret >= 0)
						{
							out.write(reinterpret_cast<char*>(packet->data), packet->size);
							continue;
						}
						else
						{
							break;
						}
					} while (true);	
				}

				//// 拷贝音频数据
				//for (int i = 0; i < ret; ++i) // 每个样本
				//{
				//	for (int j = 0; j < channels; ++j) // 每个通道
				//	{
				//		out.write(reinterpret_cast<const char*>(ff->data[j] + persize * i), persize);
				//	}
				//}
			}

			//// 拷贝音频数据
			//for (int i = 0; i < 44100 / par->channels; ++i) // 每个样本
			//{
			//	for (int j = 0; j < par->channels; ++j) // 每个通道
			//	{
			//		auto persize = av_get_bytes_per_sample(static_cast<enum AVSampleFormat>(dframe->format));
			//		sout.write(reinterpret_cast<const char*>(dframe->data[j] + persize * i), persize);
			//	}
			//}
			
			//out.write(reinterpret_cast<char*>(dframe->data[0]), static_cast<std::streamsize>(ret) * av_get_bytes_per_sample(static_cast<enum AVSampleFormat>(dframe->format)));
			//out.write(reinterpret_cast<char*>(packet->data), packet->size);
		}
	});

	std::cin.get();
	stop = true;
	if (th.joinable())
	{
		th.join();
	}

	while (av_audio_fifo_size(fifo) > 0)
	{
		ret = gff::frame_make_writable(ff);
		CHECKFFRET(ret);
		ret = av_audio_fifo_read(fifo, reinterpret_cast<void**>(ff->data), framesize);
		CHECKFFRET(ret);

		ret = enc.encode_push_frame(ff);
		CHECKFFRET(ret);
		if (ret >= 0)
		{
			do {
				ret = enc.encode_get_packet(packet);
				CHECKFFRET(ret);
				if (ret >= 0)
				{
					out.write(reinterpret_cast<char*>(packet->data), packet->size);
					continue;
				}
				else
				{
					break;
				}
			} while (true);
		}

		//// 拷贝音频数据
		//for (int i = 0; i < ret / channels; ++i) // 每个样本
		//{
		//	for (int j = 0; j < channels; ++j) // 每个通道
		//	{
		//		out.write(reinterpret_cast<const char*>(ff->data[j] + persize * i), persize);
		//	}
		//}
	}

	ret = enc.encode_push_frame(nullptr);
	CHECKFFRET(ret);
	if (ret >= 0)
	{
		do {
			ret = enc.encode_get_packet(packet);
			CHECKFFRET(ret);
			if (ret >= 0)
			{
				out.write(reinterpret_cast<char*>(packet->data), packet->size);
				continue;
			}
			else
			{
				break;
			}
		} while (true);
	}

	av_audio_fifo_free(fifo);
	out.close();
	audio.cleanup();

	return 0;
}

int test_record_video()
{
	std::string in;
	auto ret = gconvert::ansi2utf8("video=Chicony USB 2.0 Camera", in);
	CHECKFFRET(ret);

	gff::gdemux video;
	ret = video.open(in.c_str(), "dshow", { {"framerate", "30"}});
	CHECKFFRET(ret);
	const AVCodecParameters* par = nullptr;
	AVRational timebase = { 0 };
	ret = video.get_stream_par(0, par, timebase);
	CHECKFFRET(ret);

	//std::ofstream out("save.jpg", std::ios::binary);
	//std::ofstream out("save.yuv", std::ios::binary);
	std::ofstream out("save.264", std::ios::binary);
	auto packet = gff::GetPacket();
	bool stop = false;

	gff::gdec dec;
	ret = dec.copy_param(par);
	CHECKFFRET(ret);

	enum AVPixelFormat pixfmt = AV_PIX_FMT_YUV420P;
	gff::gsws sws;
	ret = sws.create_sws(static_cast<AVPixelFormat>(par->format), par->width, par->height,
		pixfmt, par->width, par->height);
	CHECKFFRET(ret);

	auto frame = gff::GetFrame();
	auto dframe = gff::GetFrame();
	ret = gff::GetFrameBuf(dframe, par->width, par->height, pixfmt, 0);
	CHECKFFRET(ret);

	gff::genc enc;
	ret = enc.set_video_param("libx264", 1000000, par->width, par->height,
		{ 1, 30 }, { 30, 1 }, 120, 60, pixfmt);
	CHECKFFRET(ret);

	static uint64_t pts = 0;

	std::thread th([&]() {
		while (video.readpacket(packet) == 0 && !stop)
		{
			//out.write(reinterpret_cast<char*>(packet->data), packet->size);
			ret = dec.decode(packet, frame);
			CHECKFFRET(ret);
		
			do 
			{
				ret = sws.scale(frame->data, frame->linesize, 0, par->height,
					dframe->data, dframe->linesize);
				CHECKFFRET(ret);
				/*out.write(reinterpret_cast<char*>(dframe->data[0]), static_cast<int64_t>(dframe->linesize[0]) * dframe->height);
				out.write(reinterpret_cast<char*>(dframe->data[1]), static_cast<int64_t>(dframe->linesize[1]) * dframe->height / 2);
				out.write(reinterpret_cast<char*>(dframe->data[2]), static_cast<int64_t>(dframe->linesize[2]) * dframe->height / 2);*/

				dframe->pts = pts++;
				ret = enc.encode_push_frame(dframe);
				CHECKFFRET(ret);

				while (ret >= 0)
				{
					ret = enc.encode_get_packet(packet);
					CHECKFFRET(ret);
					if (ret >= 0)
					{
						out.write(reinterpret_cast<char*>(packet->data), packet->size);
					}
				}

				ret = dec.decode(nullptr, frame);
			} while (ret >= 0);
			
		}
	});

	std::cin.get();
	stop = true;
	if (th.joinable())
	{
		th.join();
	}

	ret = enc.encode_push_frame(nullptr);
	CHECKFFRET(ret);
	while (ret >= 0)
	{
		ret = enc.encode_get_packet(packet);
		CHECKFFRET(ret);
		if (ret >= 0)
		{
			out.write(reinterpret_cast<char*>(packet->data), packet->size);
		}
	}

	out.close();
	video.cleanup();

	return 0;
}

int main(int argc, const char* argv[])
{
	std::cout << "hello g-ffmpeg!" << std::endl;
	//av_log_set_level(AV_LOG_TRACE);

	/// 各个媒体文件只有可以从我的RandB仓库获取，https://github.com/gongluck/RandB.git
	/// out.xxx等文件可以通过解码的例子生成

	//test_demux("gx.mkv");
	//test_dec("gx.mkv");
	//test_dec_h264("gx.h264");
	//test_enc_video("out.yuv");
	//test_enc_audio("out.pcm");
	//test_sws("out.yuv");
	//test_swr("out.pcm");
	//test_mux("out.mp4");

	//test_record_audio();
	test_record_video();

	std::cin.get();
	return 0;
}
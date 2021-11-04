#include <stdio.h>
#include <Windows.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <SDL.h>    
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}
#undef main

#define A 1000
#define B (A+500)

#define HA 200
#define HB (HA+620)
#define WA 200
#define WB (WA+620)

SDL_Renderer* renderer;
SDL_Texture* texture;
SDL_Rect rect;
const char* finName = "test3.mp4";
const char* foutName= "test.yuv";
const char* fout2Name = "testvid.mp4";

//Функция инициализации SDL
int initSDL(AVCodecContext* codecCtx)
{
	SDL_Window* window = NULL;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		fprintf(stderr, "Can't initialize SDL: %s\n", SDL_GetError());
		return -1;
	}

	window = SDL_CreateWindow(foutName, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
		640, 360, 0);
	if (!window)
	{
		fprintf(stderr, "Can't create SDL window\n");
		return -1;
	}

	rect.x = 0;
	rect.y = 0;
	rect.w = codecCtx->width;
	rect.h = codecCtx->coded_height;

	renderer = SDL_CreateRenderer(window, -1, 0);
	if (!renderer)
	{
		fprintf(stderr, "Can't create SDL renderer\n");
		return -1;
	}

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
		codecCtx->width, codecCtx->height);
	if (!texture)
	{
		fprintf(stderr, "Can't create SDL texture\n");
		return -1;
	}

	return 0;
}

//Функция записи в файл
void saveFrame(AVFrame* frame, FILE* f)
{
	uint32_t pitchY = frame->linesize[0];
	uint32_t pitchU = frame->linesize[1];
	uint32_t pitchV = frame->linesize[2];

	uint8_t* avY = frame->data[0];
	uint8_t* avU = frame->data[1];
	uint8_t* avV = frame->data[2];

	for (uint32_t i = 0; i < HB; ++i) {
		avY += WA;
		if (i >= HA) fwrite(avY, WB-WA, 1, f);
		avY += pitchY - WA;
	}

	for (uint32_t i = 0; i < HB / 2; ++i) {
		avU += WA / 2;
		if (i >= HA / 2) fwrite(avU, (WB / 2) - (WA / 2), 1, f);
		avU += pitchU - (WA / 2);
	}

	for (uint32_t i = 0; i < HB / 2; ++i) {
		avV += WA / 2;
		if (i >= HA / 2) fwrite(avV, (WB / 2) - (WA / 2), 1, f);
		avV += pitchV - (WA / 2);
	}
}

//Функция отображения кадра
void displayFrame(AVFrame* frame, AVCodecContext* decCtx)
{
	SDL_UpdateYUVTexture(texture, &rect, frame->data[0], frame->linesize[0],
		frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

//Функция декодирования
void decode(AVCodecContext* dec_ctx, AVFrame* frame, AVPacket* pkt, FILE* f)
{
	int ret;

	ret = avcodec_send_packet(dec_ctx, pkt);
	if (ret < 0) {
		fprintf(stderr, "Error sending a packet for decoding\n");
		exit(1);
	}

	while (ret >= 0) {
		ret = avcodec_receive_frame(dec_ctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			return;
		else if (ret < 0) {
			fprintf(stderr, "Error during decoding\n");
			exit(1);
		}

		
		//Sleep(1000 / fps);

		if (dec_ctx->frame_number >= A && dec_ctx->frame_number <= B)
		{
			printf("saving frame %3d\n", dec_ctx->frame_number);
			fflush(stdout);
			displayFrame(frame, dec_ctx);
			saveFrame(frame, f);
		}
	}
}


int main()
{
	FILE* fin = NULL;
	FILE* fout = NULL;
	AVFormatContext* formatCtx = NULL;
	AVCodecContext* codecCtx = NULL;
	AVCodec* codec = NULL;
	AVFrame* frame = NULL;
	AVPacket* packet = NULL;
	const char* codecName = NULL;
	int vidStreamInx = -1, ret;

	int64_t br = 0;
	int w = 0, h = 0, gs = 0, mbf = 0;
	AVRational tb, fr;
	AVPixelFormat pf;

	//Открытие исходного видеофайла
	if (avformat_open_input(&formatCtx, finName, NULL, NULL))
	{
		av_log(NULL, AV_LOG_ERROR, "Can't open input file\n");
		goto end;
	}

	//Получение информации о потоке
	if (avformat_find_stream_info(formatCtx, NULL) < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "Can't get stream info\n");
		goto end;
	}

	//Получение номера видео-потока
	for (int i = 0; i < formatCtx->nb_streams; ++i)
	{
		if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			vidStreamInx = i;
			break;
		}
	}

	if (vidStreamInx < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "No stream found\n");
		goto end;
	}

	//Вывод информации о видео-потоке
	av_dump_format(formatCtx, vidStreamInx, finName, false);

	//Выделение памяти для контекста кодека
	codecCtx = avcodec_alloc_context3(NULL);

	//Получение информации о кодеке из контекста формата
	if (avcodec_parameters_to_context(codecCtx, formatCtx->streams[vidStreamInx]->codecpar) < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "Can't get codec parameters\n");
		goto end;
	}

	//Сохранение информации для кодирования
	br = codecCtx->bit_rate;
	w = codecCtx->width;
	h = codecCtx->height;
	tb = codecCtx->time_base;
	fr = codecCtx->framerate;
	gs = codecCtx->gop_size;
	mbf = codecCtx->max_b_frames;
	pf = codecCtx->pix_fmt;

	//Поиск декодирующего кодека
	codec = avcodec_find_decoder(codecCtx->codec_id);

	if (codec == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
		goto end;
	}

	//Открытие кодека
	if (avcodec_open2(codecCtx, codec, NULL) < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "Can't open decoder\n");
		goto end;
	}

	fprintf(stderr, "\nDecoding codec is: %s\n", codec->name);

	//Инициализация пакета
	packet = av_packet_alloc();
	av_init_packet(packet);
	if (!packet)
	{
		av_log(NULL, AV_LOG_ERROR, "Can't initialize packet\n");
		goto end;
	}

	//Инициализация кадра
	frame = av_frame_alloc();
	if (!frame)
	{
		av_log(NULL, AV_LOG_ERROR, "Can't initialize frame\n");
		goto end;
	}

	//Открытие для записи результирующего файла
	fopen_s(&fout, foutName, "w");
	if (!fout)
	{
		av_log(NULL, AV_LOG_ERROR, "Can't open output file\n");
		goto end;
	}

	//Инициализация SDL
	if (initSDL(codecCtx) < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "initSDL function failure\n");
		goto end;
	}

	//system("pause");
	//Цикл чтения кадров
	while (1)
	{
		if (av_read_frame(formatCtx, packet) < 0)
		{
			av_log(NULL, AV_LOG_ERROR, "Can't read the frame\n");
			break;
		}
		if (packet->stream_index == vidStreamInx)
		{
			decode(codecCtx, frame, packet, fout);
			if (codecCtx->frame_number == B + 1)
				break;
		}
		av_packet_unref(packet);
	}

	//Сброс декодера
	decode(codecCtx, NULL, packet, fout);
	
	//Очистка и завершение программы
end:
	if (fin)
		fclose(fin);
	if (fout)
		fclose(fout);
	if (codecCtx)
		avcodec_close(codecCtx);
	if (formatCtx)
		avformat_close_input(&formatCtx);
	if (frame)
		av_frame_free(&frame);
	if (packet)
		av_packet_free(&packet);

	return 0;
}
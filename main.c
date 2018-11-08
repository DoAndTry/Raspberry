#include <alsa/asoundlib.h>
 
int main()
{
	int ret;
	snd_pcm_t *pcm_handle;
	snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
	snd_pcm_hw_params_t *hwparams;
	char *pcm_name;
 
	pcm_name = strdup("plughw:0,0");
 
	snd_pcm_hw_params_alloca(&hwparams);
 
	ret = snd_pcm_open(&pcm_handle, pcm_name, stream, 0);
	if (ret < 0) {
		printf("snd_pcm_open failed\n");
		return(-1);
	}
 
	ret = snd_pcm_hw_params_any(pcm_handle, hwparams);
	if (ret < 0) {
		printf("snd_pcm_hw_params_any failed\n");
		return(-1);
	}
 
	int rate = 44100;
	int exact_rate;
	int dir;
	int periods = 2;
	snd_pcm_uframes_t periodsize = 8192;
 
	ret = snd_pcm_hw_params_set_access(pcm_handle, hwparams, 
			SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret < 0) {
		printf("snd_pcm_hw_params_set_access failed\n");
		return(-1);
	}
 
	ret = snd_pcm_hw_params_set_format(pcm_handle, hwparams, 
			SND_PCM_FORMAT_S16_LE);
	if (ret < 0) {
		printf("snd_pcm_hw_params_set_format failed\n");
		return(-1);
	}
 
	exact_rate = rate;
	ret = snd_pcm_hw_params_set_rate_near(pcm_handle, hwparams, 
			&exact_rate, 0);
	if (ret < 0) {
		printf("snd_pcm_hw_params_set_rate_near failed\n");
		return(-1);
	}
	if (rate != exact_rate) {
		printf("The rate %d Hz is not supported by your hardware\n"
				"==> Using %d Hz instead\n", rate, exact_rate);
	}
 
	ret = snd_pcm_hw_params_set_channels(pcm_handle, hwparams, 2);
	if (ret < 0) {
		printf("snd_pcm_hw_params_set_channels failed\n");
		return(-1);
	}
 
	ret = snd_pcm_hw_params_set_periods(pcm_handle, hwparams, periods, 0);
	if (ret < 0) {
		printf("snd_pcm_hw_params_set_periods failed\n");
		return(-1);
	}
 
	ret = snd_pcm_hw_params_set_buffer_size(pcm_handle, hwparams, 
			(periodsize * periods) >> 2);
	if (ret < 0) {
		printf("snd_pcm_hw_params_set_buffer_size failed\n");
		return(-1);
	}
 
	ret = snd_pcm_hw_params(pcm_handle, hwparams);
	if (ret < 0) {
		printf("snd_pcm_hw_params failed\n");
		return(-1);
	}
 
	unsigned char *data;
	int l1, l2;
	short s1, s2;
	int frames;
 
	data = malloc(periodsize);
	frames = periodsize >> 2;
 
	for (l1 = 0; l1 < 100; l1++) {
		for (l2 = 0; l2 < frames; l2++) {
			s1 = (l2 % 128) * 100 - 5000;
			s2 = (l2 % 256) * 100 - 5000;
			data[4*l2] = (unsigned char)s1;
			data[4*l2+1] = s1 >> 8;
			data[4*l2+2] = (unsigned char)s2;
			data[4*l2+3] = s2 >> 8;
		}
		while ((ret = snd_pcm_writei(pcm_handle, data, frames)) < 0) {
			snd_pcm_prepare(pcm_handle);
			printf("<<<<<<<<<<<<<<Buffer Underrun>>>>>>>>>>>>\n");
		}
	}
 
	snd_pcm_drop(pcm_handle);
	snd_pcm_drain(pcm_handle);
 
	return 0;
}
/* PipeWire */
/* SPDX-FileCopyrightText: Copyright © 2021 Wim Taymans <wim.taymans@gmail.com> */
/* SPDX-FileCopyrightText: Copyright © 2026 Jakub Budisky <budovi.defective010@passmail.com> */
/* SPDX-License-Identifier: MIT */

#include <spa/interfaces/audio/aec.h>
#include <spa/support/log.h>
#include <spa/utils/string.h>
#include <spa/utils/names.h>
#include <spa/support/plugin.h>

#include <speex/speex_preprocess.h>
#include <speex/speex_echo.h>

#include <stdlib.h>
#include <stdint.h>

struct impl {
	struct spa_handle handle;
	struct spa_audio_aec aec;
	struct spa_log *log;

	uint32_t rate;
	uint32_t rec_channels;
	uint32_t play_channels;
	uint32_t frame_size;

	uint32_t play_buffer_frames;
	uint32_t current_play_frame;
	spx_int16_t *play_buffer;
	spx_int16_t *rec_buffer;
	spx_int16_t *out_buffer;

	SpeexEchoState *echo_state;
	SpeexPreprocessState *preprocess_state;
};

SPA_LOG_TOPIC_DEFINE_STATIC(log_topic, "spa.aec.speex");
#undef SPA_LOG_TOPIC_DEFAULT
#define SPA_LOG_TOPIC_DEFAULT &log_topic

static bool speex_get_spa_bool(const struct spa_dict *args, const char *key, bool default_value)
{
	const char *str = spa_dict_lookup(args, key);
	return str ? spa_atob(str) : default_value;
}

static uint32_t speex_get_spa_uint(struct spa_log *log,
				   const struct spa_dict *args,
				   const char *key,
				   uint32_t default_value)
{
	uint32_t value = default_value;
	const char *str = spa_dict_lookup(args, key);
	if (str && !spa_atou32(str, &value, 10)) {
		spa_log_error(log, "Error parsing `%s` as `%s`. Using the default (%" PRIu32 ")",
					  str, key, default_value);
	}

	return value;
}

static bool speex_get_spa_int_optional(struct spa_log *log,
				       const struct spa_dict *args,
				       const char *key,
				       int32_t *value)
{
	const char *str = spa_dict_lookup(args, key);
	if (!str) {
		return false;
	}
	if (!spa_atoi32(str, value, 10)) {
		spa_log_error(log, "Error parsing `%s` as `%s`. Ignoring", str, key);
		return false;
	}
	return true;
}

static void speex_deinit(struct impl *impl)
{
	free(impl->play_buffer);
	impl->play_buffer = NULL;
	free(impl->rec_buffer);
	impl->rec_buffer = NULL;
	free(impl->out_buffer);
	impl->out_buffer = NULL;

	// reversed de-initialization order
	if (impl->preprocess_state) {
		speex_preprocess_state_destroy(impl->preprocess_state);
		impl->preprocess_state = NULL;
	}
	if (impl->echo_state) {
		speex_echo_state_destroy(impl->echo_state);
		impl->echo_state = NULL;
	}
}

static int
speex_init2(void *object,
	    const struct spa_dict *args,
	    struct spa_audio_info_raw *rec_info,
	    struct spa_audio_info_raw *out_info,
	    struct spa_audio_info_raw *play_info)
{
	struct impl *impl = object;
	if (rec_info->channels != out_info->channels) {
		spa_log_error(impl->log, "Error: Number of recording and output channels must match");
		return -EINVAL;
	}
	if (rec_info->rate != out_info->rate || rec_info->rate != play_info->rate) {
		spa_log_error(impl->log, "Error: Rates of all the streams must match");
		return -EINVAL;
	}

	impl->rate = rec_info->rate;
	impl->rec_channels = rec_info->channels;
	impl->play_channels = play_info->channels;

	uint32_t frame_length = speex_get_spa_uint(impl->log, args, "speex.frame_length", 10);
	uint32_t frame_samples = impl->rate * frame_length / 1000;
	uint32_t filter_length = speex_get_spa_uint(impl->log, args, "speex.filter_length", 100);
	uint32_t filter_samples = frame_samples * (filter_length / frame_length);
	uint32_t filter_delay = speex_get_spa_uint(impl->log, args, "speex.filter_delay", 20);

	spa_log_warn(impl->log, "Picking frame length %" PRIu32 "ms (%" PRIu32 " samples), "
				"filter length %" PRIu32 "ms, and filter delay %" PRIu32 "ms",
		     frame_length, frame_samples, filter_length, filter_delay);
	if (!filter_samples) {
		spa_log_error(impl->log, "Error: The frame size must be non-zero");
		return -EINVAL;
	}

	impl->frame_size = frame_samples;
	impl->play_buffer_frames = (filter_delay / frame_length) + 1;
	impl->current_play_frame = 0;
	impl->play_buffer = calloc(frame_samples * impl->play_buffer_frames * impl->play_channels,
				   sizeof(spx_int16_t));
	impl->rec_buffer = calloc(frame_samples * impl->rec_channels, sizeof(spx_int16_t));
	impl->out_buffer = calloc(frame_samples * impl->rec_channels, sizeof(spx_int16_t));
	if (!impl->play_buffer || !impl->rec_buffer || !impl->out_buffer) {
		speex_deinit(impl);
		spa_log_error(impl->log, "Error: Failed to allocate the buffer(s)");
		return -ENOMEM;
	}

	impl->echo_state = speex_echo_state_init_mc(frame_samples, filter_samples,
						    impl->rec_channels, impl->play_channels);
	if (!impl->echo_state) {
		speex_deinit(impl);
		spa_log_error(impl->log, "Error: Failed to initialize SPEEX AEC");
		return -EBADE;
	}
	int sample_rate = impl->rate;
	if (speex_echo_ctl(impl->echo_state, SPEEX_ECHO_SET_SAMPLING_RATE, &sample_rate)) {
		spa_log_warn(impl->log, "Sample rate could not be set for SPEEX AEC, expect issues");
	}

	if (!speex_get_spa_bool(args, "speex.preprocess", false))
		return 0;

	if (rec_info->channels != 1) {
		speex_deinit(impl);
		spa_log_error(impl->log, "Error: Preprocessing not supported for a "
					 "multi-channel recording");
		return -EINVAL;
	}
	impl->preprocess_state = speex_preprocess_state_init(frame_samples, impl->rate);
	if (!impl->preprocess_state) {
		speex_deinit(impl);
		spa_log_error(impl->log, "Error: Failed to initialize preprocessor");
		return -EBADE;
	}
	if (speex_preprocess_ctl(impl->preprocess_state, SPEEX_PREPROCESS_SET_ECHO_STATE,
					impl->echo_state)) {
		speex_deinit(impl);
		spa_log_error(impl->log, "Error: Failed to associate the preprocessor with "
					 "the echo canceller");
		return -EBADE;
	}

	int error_counter = 0;
	spx_int32_t enable_value;
	int32_t level_value;

	// Residual echo suppression
	if (speex_get_spa_int_optional(impl->log, args, "speex.preprocess.echo_suppress",
				       &level_value)) {
		error_counter += speex_preprocess_ctl(
			impl->preprocess_state, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &level_value);
	}
	if (!speex_preprocess_ctl(impl->preprocess_state, SPEEX_PREPROCESS_GET_ECHO_SUPPRESS,
				  &level_value)) {
		spa_log_warn(impl->log, "Residual echo suppression limit set to %d dB",
			     level_value);
	}

	// Noise suppression
	if (speex_get_spa_bool(args, "speex.preprocess.denoise", false)) {
		enable_value = 1;
		error_counter += speex_preprocess_ctl(
			impl->preprocess_state, SPEEX_PREPROCESS_SET_DENOISE, &enable_value);

		if (speex_get_spa_int_optional(
		    impl->log, args, "speex.preprocess.noise_suppress", &level_value)) {
			error_counter += speex_preprocess_ctl(
				impl->preprocess_state,
				SPEEX_PREPROCESS_SET_NOISE_SUPPRESS,
				&level_value
			);
		}
		if (!speex_preprocess_ctl(impl->preprocess_state,
					  SPEEX_PREPROCESS_GET_NOISE_SUPPRESS,
					  &level_value)) {
			spa_log_warn(impl->log, "Noise suppression limit set to %d dB",
				     level_value);
		}
	} else {
		enable_value = 2;
		error_counter += speex_preprocess_ctl(
			impl->preprocess_state, SPEEX_PREPROCESS_SET_DENOISE, &enable_value);
	}

	// Automatic gain control (always off)
	enable_value = 2;
	error_counter += speex_preprocess_ctl(
		impl->preprocess_state, SPEEX_PREPROCESS_SET_AGC, &enable_value);

	// Voice activity detector (a "hack", according to the Speex DSP authors)
	if (speex_get_spa_bool(args, "speex.preprocess.voice_activity_detector", false)) {
		enable_value = 1;
		error_counter += speex_preprocess_ctl(
			impl->preprocess_state, SPEEX_PREPROCESS_SET_VAD, &enable_value);

		if (speex_get_spa_int_optional(
		    impl->log, args, "speex.preprocess.echo_suppress_active", &level_value)) {
			error_counter += speex_preprocess_ctl(
				impl->preprocess_state,
				SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE,
				&level_value
			);
		}
		if (!speex_preprocess_ctl(impl->preprocess_state,
					  SPEEX_PREPROCESS_GET_ECHO_SUPPRESS_ACTIVE,
					  &level_value)) {
			spa_log_warn(impl->log, "Active echo suppression limit set to %d dB",
				     level_value);
		}
	} else {
		enable_value = 2;
		error_counter += speex_preprocess_ctl(
			impl->preprocess_state, SPEEX_PREPROCESS_SET_VAD, &enable_value);
	}

	// Dereverbiation
	enable_value = speex_get_spa_bool(args, "speex.preprocess.dereverb", false) ? 1 : 2;
	error_counter += speex_preprocess_ctl(
		impl->preprocess_state, SPEEX_PREPROCESS_SET_DEREVERB, &enable_value);

	if (error_counter) {
		spa_log_warn(impl->log, "%d preprocess control calls failed", -error_counter);
	}

	return 0;
}

static int
speex_init(void *object, const struct spa_dict *args, const struct spa_audio_info_raw *info)
{
	struct spa_audio_info_raw mutable_info = *info;
	return speex_init2(object, args, &mutable_info, &mutable_info, &mutable_info);
}

static void speex_copy_in(const float *src[], spx_int16_t *dst, uint32_t channels, uint32_t samples)
{
	const float factor = (float) INT16_MAX;
	for (uint32_t sample = 0; sample < samples; sample++)
		for (uint32_t channel = 0; channel < channels; channel++) {
			dst[sample * channels + channel] = (spx_int16_t)(src[channel][sample] * factor);
		}
}

static void speex_copy_out(const spx_int16_t *src, float *dst[], uint32_t channels, uint32_t samples)
{
	const float factor = (float) INT16_MAX;
	for (uint32_t sample = 0; sample < samples; sample++)
		for (uint32_t channel = 0; channel < channels; channel++) {
			dst[channel][sample] = src[sample * channels + channel] / factor;
		}
}

static int
speex_run(void *object, const float *rec[], const float *play[], float *out[], uint32_t n_samples)
{
	struct impl *impl = object;
	// Check that the number of samples is equal to the frame size
	if (n_samples != impl->frame_size) {
		spa_log_error(impl->log, "Error: The quantum must be set to match the frame size "
					 "(%" PRIu32 ", got %" PRIu32 ")",
			      impl->frame_size, n_samples);
		return -EINVAL;
	}

	// Write the interleaved play buffer into the current slot
	spx_int16_t *play_buffer = impl->play_buffer;
	play_buffer += impl->current_play_frame * (impl->frame_size * impl->play_channels);
	speex_copy_in(play, play_buffer, impl->play_channels, impl->frame_size);

	// Move the "pointer" to the current buffer to the next (and the last) one
	impl->current_play_frame = (impl->current_play_frame + 1) % impl->play_buffer_frames;
	play_buffer = impl->play_buffer;
	play_buffer += impl->current_play_frame * (impl->frame_size * impl->play_channels);

	// Write the interleaved recording buffer into its buffer
	speex_copy_in(rec, impl->rec_buffer, impl->rec_channels, impl->frame_size);

	// Call the library and copy out the resulting signal
	speex_echo_cancellation(impl->echo_state, impl->rec_buffer, play_buffer, impl->out_buffer);
	if (impl->preprocess_state)
		speex_preprocess_run(impl->preprocess_state, impl->out_buffer);
	speex_copy_out(impl->out_buffer, out, impl->rec_channels, impl->frame_size);

	return 0;
}

static const struct spa_audio_aec_methods impl_aec = {
	.version = SPA_VERSION_AUDIO_AEC_METHODS,
	.add_listener = NULL,
	.init = speex_init,
	.run = speex_run,
	.init2 = speex_init2,
};

static int impl_get_interface(struct spa_handle *handle, const char *type, void **interface)
{
	spa_return_val_if_fail(handle != NULL, -EINVAL);
	spa_return_val_if_fail(interface != NULL, -EINVAL);

	struct impl *impl = (struct impl *) handle;

	if (spa_streq(type, SPA_TYPE_INTERFACE_AUDIO_AEC))
		*interface = &impl->aec;
	else
		return -ENOENT;

	return 0;
}

static int impl_clear(struct spa_handle *handle)
{
	spa_return_val_if_fail(handle != NULL, -EINVAL);

	struct impl *impl = (struct impl *) handle;
	speex_deinit(impl);

	return 0;
}

static size_t
impl_get_size(const struct spa_handle_factory *factory,
	      const struct spa_dict *params)
{
	return sizeof(struct impl);
}

static int
impl_init(const struct spa_handle_factory *factory,
	  struct spa_handle *handle,
	  const struct spa_dict *info,
	  const struct spa_support *support,
	  uint32_t n_support)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(handle != NULL, -EINVAL);

	handle->get_interface = impl_get_interface;
	handle->clear = impl_clear;

	struct impl *impl = (struct impl *) handle;

	impl->aec.iface = SPA_INTERFACE_INIT(
		SPA_TYPE_INTERFACE_AUDIO_AEC,
		SPA_VERSION_AUDIO_AEC,
		&impl_aec, impl);
	impl->aec.name = "speex";
	impl->aec.info = NULL;
	impl->aec.latency = "480/48000";

	impl->play_buffer = NULL;
	impl->rec_buffer = NULL;
	impl->out_buffer = NULL;
	impl->echo_state = NULL;
	impl->preprocess_state = NULL;

	impl->log = spa_support_find(support, n_support, SPA_TYPE_INTERFACE_Log);
	spa_log_topic_init(impl->log, &log_topic);

	return 0;
}

static const struct spa_interface_info impl_interfaces[] = {
	{SPA_TYPE_INTERFACE_AUDIO_AEC,},
};

static int
impl_enum_interface_info(const struct spa_handle_factory *factory,
			 const struct spa_interface_info **info,
			 uint32_t *index)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(info != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	switch (*index) {
	case 0:
		*info = &impl_interfaces[*index];
		break;
	default:
		return 0;
	}
	(*index)++;
	return 1;
}

static const struct spa_handle_factory spa_aec_speex_factory = {
	SPA_VERSION_HANDLE_FACTORY,
	SPA_NAME_AEC,
	NULL,
	impl_get_size,
	impl_init,
	impl_enum_interface_info,
};

SPA_LOG_TOPIC_ENUM_DEFINE_REGISTERED;

SPA_EXPORT
int spa_handle_factory_enum(const struct spa_handle_factory **factory, uint32_t *index)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	switch (*index) {
	case 0:
		*factory = &spa_aec_speex_factory;
		break;
	default:
		return 0;
	}
	(*index)++;
	return 1;
}

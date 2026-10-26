#include "seyrix.h"

#include <stdlib.h>
#include <time.h>

#include <wlr/backend.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "layer-shell.h"
#include "shell.h"
#include "window-state.h"

static const float background_color[4] = {0.035f, 0.075f, 0.13f, 1.0f};

static void remove_listener(struct wl_listener *listener) {
	if (listener->link.prev != NULL && listener->link.next != NULL) {
		wl_list_remove(&listener->link);
		wl_list_init(&listener->link);
	}
}

static struct seyrix_output *output_record_for_wlr_output(
		struct seyrix_server *server, struct wlr_output *wlr_output) {
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output == wlr_output) {
			return output;
		}
	}
	return NULL;
}

static void output_manager_update(struct seyrix_server *server) {
	if (server->output_manager == NULL || server->output_layout == NULL) {
		return;
	}
	struct wlr_output_configuration_v1 *configuration =
		wlr_output_configuration_v1_create();
	if (configuration == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate current output configuration");
		return;
	}
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_output_configuration_head_v1 *head =
			wlr_output_configuration_head_v1_create(configuration,
				output->wlr_output);
		if (head == NULL) {
			wlr_output_configuration_v1_destroy(configuration);
			wlr_log(WLR_ERROR, "Cannot add output %s to manager configuration",
				output->wlr_output->name);
			return;
		}
		double x = 0.0;
		double y = 0.0;
		wlr_output_layout_output_coords(server->output_layout,
			output->wlr_output, &x, &y);
		head->state.x = (int32_t)x;
		head->state.y = (int32_t)y;
		head->state.enabled = output->wlr_output->enabled;
		head->state.scale = output->wlr_output->scale;
		head->state.transform = output->wlr_output->transform;
		head->state.adaptive_sync_enabled =
			output->wlr_output->adaptive_sync_status ==
				WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED;
	}
	wlr_output_manager_v1_set_configuration(server->output_manager,
		configuration);
}

static bool output_configuration_is_complete(struct seyrix_server *server,
		struct wlr_output_configuration_v1 *configuration) {
	size_t output_count = 0;
	size_t head_count = 0;
	size_t enabled_count = 0;
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		output_count++;
	}
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &configuration->heads, link) {
		if (output_record_for_wlr_output(server, head->state.output) == NULL) {
			return false;
		}
		struct wlr_output_configuration_head_v1 *other;
		wl_list_for_each(other, &configuration->heads, link) {
			if (other == head) {
				break;
			}
			if (other->state.output == head->state.output) {
				return false;
			}
		}
		head_count++;
		if (head->state.enabled) {
			enabled_count++;
		}
	}
	return head_count == output_count && enabled_count > 0;
}

static bool apply_output_configuration(struct seyrix_server *server,
		struct wlr_output_configuration_v1 *configuration, bool apply) {
	if (!output_configuration_is_complete(server, configuration)) {
		wlr_log(WLR_ERROR, "Rejecting incomplete or invalid output configuration");
		return false;
	}
	size_t states_len = 0;
	struct wlr_backend_output_state *states =
		wlr_output_configuration_v1_build_state(configuration, &states_len);
	if (states == NULL || states_len == 0 ||
			!wlr_backend_test(server->backend, states, states_len)) {
		free(states);
		wlr_log(WLR_ERROR, "Output configuration failed backend validation");
		return false;
	}
	if (!apply) {
		free(states);
		return true;
	}
	server->output_manager_applying = true;
	if (!wlr_backend_commit(server->backend, states, states_len)) {
		server->output_manager_applying = false;
		free(states);
		wlr_log(WLR_ERROR, "Backend rejected a tested output configuration");
		return false;
	}
	free(states);

	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &configuration->heads, link) {
		if (head->state.enabled) {
			wlr_output_layout_add(server->output_layout, head->state.output,
				head->state.x, head->state.y);
		} else {
			wlr_output_layout_remove(server->output_layout,
				head->state.output);
		}
	}
	server->output_manager_applying = false;
	output_manager_update(server);
	wlr_log(WLR_INFO, "Applied output configuration with %zu heads",
		states_len);
	return true;
}

static void handle_output_manager_test(struct wl_listener *listener, void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, output_manager_test);
	struct wlr_output_configuration_v1 *configuration = data;
	if (apply_output_configuration(server, configuration, false)) {
		wlr_output_configuration_v1_send_succeeded(configuration);
	} else {
		wlr_output_configuration_v1_send_failed(configuration);
	}
	wlr_output_configuration_v1_destroy(configuration);
}

static void handle_output_manager_apply(struct wl_listener *listener, void *data) {
	struct seyrix_server *server =
		wl_container_of(listener, server, output_manager_apply);
	struct wlr_output_configuration_v1 *configuration = data;
	if (apply_output_configuration(server, configuration, true)) {
		wlr_output_configuration_v1_send_succeeded(configuration);
	} else {
		wlr_output_configuration_v1_send_failed(configuration);
	}
	wlr_output_configuration_v1_destroy(configuration);
}

static void position_backgrounds(struct seyrix_server *server) {
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		double x = 0.0;
		double y = 0.0;
		wlr_output_layout_output_coords(server->output_layout,
			output->wlr_output, &x, &y);
		wlr_scene_node_set_position(&output->background->node, (int)x, (int)y);
	}
}

static void handle_layout_change(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_server *server =
		wl_container_of(listener, server, output_layout_change);
	position_backgrounds(server);
	seyrix_layer_shell_outputs_changed(server);
	seyrix_wm_outputs_changed(server);
	seyrix_shell_outputs_changed(server);
	seyrix_window_state_outputs_changed(server);
	if (!server->output_manager_applying) {
		output_manager_update(server);
	}
}

static void handle_output_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_output *output = wl_container_of(listener, output, frame);
	if (!wlr_scene_output_commit(output->scene_output, NULL)) {
		wlr_log(WLR_ERROR, "Output %s failed to render a frame",
			output->wlr_output->name);
		return;
	}
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
		wlr_scene_output_send_frame_done(output->scene_output, &now);
	}
}

static void handle_output_request_state(struct wl_listener *listener, void *data) {
	struct seyrix_output *output =
		wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	if (!wlr_output_commit_state(output->wlr_output, event->state)) {
		wlr_log(WLR_ERROR, "Output %s rejected requested state",
			output->wlr_output->name);
	} else if (!output->server->output_manager_applying) {
		output_manager_update(output->server);
	}
}

static void handle_output_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_output *output = wl_container_of(listener, output, commit);
	int width = 0;
	int height = 0;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);
	float scale = output->wlr_output->scale;
	if (width <= 0 || height <= 0 ||
			(width == output->width && height == output->height &&
			scale == output->scale)) {
		if (!output->server->output_manager_applying) {
			output_manager_update(output->server);
		}
		return;
	}
	output->width = width;
	output->height = height;
	output->scale = scale;
	wlr_scene_rect_set_size(output->background, width, height);
	wlr_log(WLR_INFO, "Output %s active at %dx%d",
		output->wlr_output->name, width, height);
	seyrix_shell_outputs_changed(output->server);
	seyrix_layer_shell_outputs_changed(output->server);
	seyrix_wm_outputs_changed(output->server);
	seyrix_window_state_outputs_changed(output->server);
	if (!output->server->output_manager_applying) {
		output_manager_update(output->server);
	}
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_output *output = wl_container_of(listener, output, destroy);
	struct seyrix_server *server = output->server;
	remove_listener(&output->frame);
	remove_listener(&output->request_state);
	remove_listener(&output->commit);
	remove_listener(&output->destroy);
	struct wlr_output *removed_output = output->wlr_output;
	if (output->background != NULL && server->scene != NULL) {
		wlr_scene_node_destroy(&output->background->node);
	}
	wl_list_remove(&output->link);
	wl_list_init(&output->link);
	seyrix_layer_shell_output_removed(server, removed_output);
	seyrix_window_state_output_removed(server, removed_output);
	if (server->output_layout != NULL) {
		wlr_output_layout_remove(server->output_layout, output->wlr_output);
	}
	seyrix_wm_outputs_changed(server);
	seyrix_window_state_outputs_changed(server);
	seyrix_shell_outputs_changed(server);
	output_manager_update(server);
	wlr_log(WLR_INFO, "Output %s removed", output->wlr_output->name);
	free(output);

	if (server->backend_started && wl_list_empty(&server->outputs)) {
		wlr_log(WLR_ERROR, "No outputs remain; stopping compositor");
		wl_display_terminate(server->display);
	}
}

static void handle_new_output(struct wl_listener *listener, void *data) {
	struct seyrix_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
		wlr_log(WLR_ERROR, "Cannot initialize rendering for output %s",
			wlr_output->name);
		return;
	}

	struct seyrix_output *output = calloc(1, sizeof(*output));
	if (output == NULL) {
		wlr_log(WLR_ERROR, "Cannot allocate state for output %s",
			wlr_output->name);
		return;
	}
	output->server = server;
	output->wlr_output = wlr_output;

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}
	if (!wlr_output_commit_state(wlr_output, &state)) {
		wlr_output_state_finish(&state);
		wlr_log(WLR_ERROR, "Cannot enable output %s", wlr_output->name);
		free(output);
		return;
	}
	wlr_output_state_finish(&state);

	struct wlr_output_layout_output *layout_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
	output->background = wlr_scene_rect_create(server->background_layer,
		wlr_output->width, wlr_output->height, background_color);
	if (layout_output == NULL || output->scene_output == NULL ||
			output->background == NULL) {
		wlr_log(WLR_ERROR, "Cannot add output %s to compositor scene",
			wlr_output->name);
		if (output->background != NULL) {
			wlr_scene_node_destroy(&output->background->node);
		}
		if (output->scene_output != NULL) {
			wlr_scene_output_destroy(output->scene_output);
		}
		wlr_output_layout_remove(server->output_layout, wlr_output);
		free(output);
		return;
	}
	wlr_scene_output_layout_add_output(server->scene_layout, layout_output,
		output->scene_output);

	output->frame.notify = handle_output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->request_state.notify = handle_output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);
	output->commit.notify = handle_output_commit;
	wl_signal_add(&wlr_output->events.commit, &output->commit);
	output->destroy.notify = handle_output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	wl_list_insert(&server->outputs, &output->link);

	position_backgrounds(server);
	handle_output_commit(&output->commit, NULL);
	seyrix_shell_outputs_changed(server);
	seyrix_layer_shell_outputs_changed(server);
	seyrix_wm_outputs_changed(server);
	seyrix_window_state_outputs_changed(server);
	output_manager_update(server);
	wlr_output_schedule_frame(wlr_output);
}

bool seyrix_output_init(struct seyrix_server *server) {
	server->output_manager = wlr_output_manager_v1_create(server->display);
	if (server->output_manager == NULL) {
		wlr_log(WLR_ERROR, "Cannot create wlr-output-management-v1 global");
		return false;
	}
	server->output_manager_test.notify = handle_output_manager_test;
	wl_signal_add(&server->output_manager->events.test,
		&server->output_manager_test);
	server->output_manager_apply.notify = handle_output_manager_apply;
	wl_signal_add(&server->output_manager->events.apply,
		&server->output_manager_apply);
	server->new_output.notify = handle_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);
	server->output_layout_change.notify = handle_layout_change;
	wl_signal_add(&server->output_layout->events.change,
		&server->output_layout_change);
	return true;
}

void seyrix_output_finish(struct seyrix_server *server) {
	remove_listener(&server->new_output);
	remove_listener(&server->output_layout_change);
	remove_listener(&server->output_manager_test);
	remove_listener(&server->output_manager_apply);
	server->output_manager = NULL;
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		remove_listener(&output->frame);
		remove_listener(&output->request_state);
		remove_listener(&output->commit);
	}
}

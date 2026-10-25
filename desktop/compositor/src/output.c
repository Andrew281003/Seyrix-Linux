#include "seyrix.h"

#include <stdlib.h>
#include <time.h>

#include <wlr/util/box.h>
#include <wlr/util/log.h>

static const float background_color[4] = {0.035f, 0.075f, 0.13f, 1.0f};

static void remove_listener(struct wl_listener *listener) {
	if (listener->link.prev != NULL && listener->link.next != NULL) {
		wl_list_remove(&listener->link);
		wl_list_init(&listener->link);
	}
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
	}
}

static void handle_output_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_output *output = wl_container_of(listener, output, commit);
	int width = 0;
	int height = 0;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);
	if (width <= 0 || height <= 0 ||
			(width == output->width && height == output->height)) {
		return;
	}
	output->width = width;
	output->height = height;
	wlr_scene_rect_set_size(output->background, width, height);
	wlr_log(WLR_INFO, "Output %s active at %dx%d",
		output->wlr_output->name, width, height);
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct seyrix_output *output = wl_container_of(listener, output, destroy);
	struct seyrix_server *server = output->server;
	remove_listener(&output->frame);
	remove_listener(&output->request_state);
	remove_listener(&output->commit);
	remove_listener(&output->destroy);
	if (output->background != NULL && server->scene != NULL) {
		wlr_scene_node_destroy(&output->background->node);
	}
	wl_list_remove(&output->link);
	wl_list_init(&output->link);
	if (server->output_layout != NULL) {
		wlr_output_layout_remove(server->output_layout, output->wlr_output);
	}
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
	wlr_output_schedule_frame(wlr_output);
}

bool seyrix_output_init(struct seyrix_server *server) {
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
	struct seyrix_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		remove_listener(&output->frame);
		remove_listener(&output->request_state);
		remove_listener(&output->commit);
	}
}

#include "app_state.h"
#include "app_logic.h"
#include "app_view.h"

#include "hlea_runtime.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace hle_audio {
namespace editor {

struct app_state_t {
    // view state is mutable within widget processing pass
    view_state_t view_state;

    std::string data_file_path;
    std::string wav_path;

    // wav file list
    std::vector<fs::path> wav_files;
    std::vector<std::string> wav_files_u8_names;

    // player context
    hlea_context_t* runtime_ctx;
    hlea_event_bank_t* bank = nullptr;
    size_t bank_cmd_index = 0;

    logic_state_t bl_state;
    size_t save_cmd_index = 0;
};

static void update_active_group(app_state_t* state, size_t selected_group) {
    auto& view_state = state->view_state;

    view_state.active_group_index = selected_group;

    if (selected_group == invalid_index) return;


    view_state.selected_group_state = get_group(&state->bl_state.data_state, selected_group);
}

static void update_selected_event_action(app_state_t* state, size_t action_index) {
    auto& view_state = state->view_state;

    if (view_state.active_event_index == invalid_index) return;

    auto& event = state->bl_state.data_state.events[view_state.active_event_index];

    if (event.actions.size()) {
        action_index = event.actions.size() <= action_index ? event.actions.size() - 1 : action_index;
        view_state.active_action_index = action_index;
        view_state.active_action = *event.actions[view_state.active_action_index];
    }
}

static void update_active_event(app_state_t* state, size_t active_index) {
    auto& view_state = state->view_state;

    view_state.active_event_index = active_index;
    
    if (active_index == invalid_index) return;

    auto& event = state->bl_state.data_state.events[active_index];

    snprintf(view_state.event_state.name, sizeof(view_state.event_state.name), 
            "%s", event.name.c_str());

    update_selected_event_action(state, 0u);
}

static void filter_events(app_state_t* state) {
    auto& view_state = state->view_state;

    view_state.filtered_event_indices.clear();
    view_state.event_list_index = invalid_index;

    const auto& events = state->bl_state.data_state.events;
    for (size_t event_index = 0; event_index < events.size(); ++event_index) {
        auto& event = events[event_index];

        // skip if name doesn't match
        if (event.name.find(view_state.event_filter_str) == std::string::npos) continue;

        // filter group
        if (view_state.event_filter_group_index != invalid_index &&
            !is_event_target_group(event, view_state.event_filter_group_index)) continue;

        view_state.filtered_event_indices.push_back(event_index);

        if (view_state.active_event_index == event_index) {
            view_state.event_list_index = view_state.filtered_event_indices.size() - 1;
        }
    }
}

static void update_mutable_view_state(app_state_t* state) {
    auto& view_state = state->view_state;
    auto& data_state = state->bl_state.data_state;

    auto groups_size = data_state.groups.size();

    // reset event group filter if groups list changed
    if (view_state.groups_size_on_event_filter_group != groups_size)
        view_state.event_filter_group_index = invalid_index;

    auto selected_group = view_state.active_group_index;
    if (groups_size == 0u) {
        selected_group = invalid_index;
    } else if (selected_group != invalid_index && 
        groups_size <= selected_group) {
        selected_group = groups_size - 1;
    }
    update_active_group(state, selected_group);

    auto active_action_index = view_state.active_action_index;

    auto selected_event_index = view_state.active_event_index;
    if (data_state.events.size() == 0u) {
        selected_event_index = invalid_index;
    }
    update_active_event(state, selected_event_index);
    update_selected_event_action(state, active_action_index);

    filter_events(state);
}

static void perform_undo(app_state_t* state) {
    if (apply_undo_chain(&state->bl_state.cmds, &state->bl_state.data_state)) {
        update_mutable_view_state(state);
    }
}

static void perform_redo(app_state_t* state) {
    if (apply_redo_chain(&state->bl_state.cmds, &state->bl_state.data_state)) {
        update_mutable_view_state(state);
    }
}

static void refresh_wav_list(app_state_t* state) {
    state->wav_files.clear();
    state->wav_files_u8_names.clear();

    for (const auto & entry : fs::directory_iterator(state->wav_path)) {
        if (entry.path().extension() == ".wav") {
            state->wav_files.push_back(entry.path());
            state->wav_files_u8_names.push_back(entry.path().filename().u8string());
        }
    }
}

static void create_context(app_state_t* state) {
    auto& data_state = state->bl_state.data_state;

    hlea_context_create_info_t ctx_info = {};
    ctx_info.output_bus_count = data_state.output_buses.size();
    state->runtime_ctx = hlea_create(&ctx_info);
}

static void unload_and_destroy_context(app_state_t* state) {
    if (state->bank) {
        hlea_unload_events_bank(state->runtime_ctx, state->bank);
        state->bank = nullptr;
    }
    hlea_stop_file(state->runtime_ctx);
    hlea_destroy(state->runtime_ctx);
    state->runtime_ctx = nullptr;
}

static void recreate_context(app_state_t* state) {
    unload_and_destroy_context(state);
    create_context(state);
}

static void add_output_bus(app_state_t* state) {
    auto& data_state = state->bl_state.data_state;

    output_bus_t bus = {};
    bus.name = "<bus name>";
    data_state.output_buses.push_back(bus);

    // todo: add cmd

    state->view_state.output_bus_volumes.push_back(100);

    recreate_context(state);
}

app_state_t* create_app_state(float scale) {
    auto res = new app_state_t;

    init(&res->bl_state);
    
    // setup view
    res->view_state.scale = scale;
    res->view_state.root_pane_width_scaled = 200 * scale;

    return res;
}

void destroy(app_state_t* state) {
    unload_and_destroy_context(state);
    delete state;
}

void init_with_data(app_state_t* state, const char* filepath, const char* wav_folder) {
    state->data_file_path = filepath;
    state->wav_path = wav_folder;
    refresh_wav_list(state);

    load_store_json(&state->bl_state.data_state, state->data_file_path.c_str());

    // update view output buses volumes
    state->view_state.output_bus_volumes.clear();
    state->view_state.output_bus_volumes.insert(
        state->view_state.output_bus_volumes.end(), 
        state->bl_state.data_state.output_buses.size(), 100);

    create_context(state);

    hlea_set_wav_path(state->runtime_ctx, state->wav_path.c_str());

    update_mutable_view_state(state);
}

static void fire_event(app_state_t* state) {
    if (state->bank && state->bank_cmd_index != get_undo_size(&state->bl_state.cmds)) {
        hlea_unload_events_bank(state->runtime_ctx, state->bank);
        state->bank = nullptr;
    }
    if (!state->bank) {
        auto fb_buffer = save_store_fb_buffer(&state->bl_state.data_state);
        state->bank = hlea_load_events_bank_from_buffer(state->runtime_ctx, fb_buffer.data(), fb_buffer.size());
        state->bank_cmd_index = get_undo_size(&state->bl_state.cmds);
    }

    auto& event_name = state->bl_state.data_state.events[state->view_state.active_event_index].name;
    hlea_fire_event(state->runtime_ctx, state->bank, event_name.c_str(), 0u);
}

void process_frame(app_state_t* state) {
    /**
     *  update runtime
     */
    hlea_process_active_groups(state->runtime_ctx);

    /**
     * process view
     */
    auto& view_state = state->view_state;

    const auto prev_group_index = view_state.active_group_index;
    const auto prev_event_index = view_state.event_list_index;
    const auto prev_action_index = view_state.active_action_index;

    //
    // build up view
    //
    view_state.has_save = state->save_cmd_index != get_undo_size(&state->bl_state.cmds);
    view_state.has_undo = has_undo(&state->bl_state.cmds);
    view_state.has_redo = has_redo(&state->bl_state.cmds);
    view_state.has_wav_playing = hlea_is_file_playing(state->runtime_ctx);
    view_state.wav_files_u8_names_ptr = &state->wav_files_u8_names;

    auto action = build_view(view_state, state->bl_state.data_state);

    //
    // modify view state
    //
    if (prev_group_index != view_state.active_group_index)
        update_active_group(state, view_state.active_group_index);

    if (prev_event_index != view_state.event_list_index) {
        auto selected_event_index = view_state.filtered_event_indices[view_state.event_list_index];
        update_active_event(state, selected_event_index);
    }

    if (prev_action_index != view_state.active_action_index)
        update_selected_event_action(state, view_state.active_action_index);

    //
    // modify data state
    //
    const auto& event_state = view_state.event_state;
    auto bl_state = &state->bl_state;
    switch (action)
    {
    case view_action_type_e::SAVE: {
        save_store_json(&bl_state->data_state, state->data_file_path.c_str());
        state->save_cmd_index = get_undo_size(&state->bl_state.cmds);
        break;
    }
    case view_action_type_e::UNDO:
        perform_undo(state);
        break;
    case view_action_type_e::REDO:
        perform_redo(state);
        break;

    case view_action_type_e::GROUP_ADD: {
        auto new_group_index = view_state.action_group_index + 1;
        create_group(bl_state, new_group_index); // todo support create before/after
        update_active_group(state, new_group_index);
        update_mutable_view_state(state);
        break;
    }
    case view_action_type_e::GROUP_REMOVE:
        remove_group(bl_state, view_state.action_group_index);
        update_mutable_view_state(state);
        break;
    case view_action_type_e::APPLY_SELECTED_GROUP_UPDATE: {
        apply_group_update(bl_state, view_state.active_group_index, 
                view_state.selected_group_state);
        update_mutable_view_state(state);
        break;
    }

    case view_action_type_e::EVENT_ADD: {
        auto new_index = view_state.active_event_index + 1;
        create_event(bl_state, new_index);
        update_active_event(state, new_index);
        update_mutable_view_state(state);
        break;
    }
    case view_action_type_e::EVENT_REMOVE:
        remove_event(bl_state, view_state.active_event_index);
        update_mutable_view_state(state);
        break;
    case view_action_type_e::EVENT_UPDATE:
        update_event(bl_state, view_state.active_event_index, event_state.name);
        update_mutable_view_state(state);
        break;
    case view_action_type_e::EVENT_FILTER:
        filter_events(state);
        break;
    case view_action_type_e::EVENT_UPDATE_ACTION:
        update_event_action(bl_state, 
            view_state.active_event_index, 
            view_state.active_action_index, 
            view_state.active_action);
        break;
    case view_action_type_e::EVENT_REMOVE_ACTION:
        remove_event_action(bl_state, 
            view_state.active_event_index, 
            view_state.active_action_index);
        update_selected_event_action(state, 
            view_state.active_action_index);
        break;
    case view_action_type_e::EVENT_APPEND_ACTION: {
        auto new_action_index = add_event_action(bl_state, 
            view_state.active_event_index, view_state.active_group_index);
        update_selected_event_action(state, new_action_index);
        break;
    }

    case view_action_type_e::NODE_ADD: {
        if (view_state.add_node_type) {
            if (view_state.add_node_target.type == NodeType_None) {
                create_root_node(bl_state, 
                    view_state.active_group_index,
                    view_state.add_node_type);
            } else if (view_state.add_node_target.type == NodeType_Repeat) {
                create_repeat_node(bl_state, view_state.add_node_target, view_state.add_node_type);
            } else {
                // this is add child node
                create_node(bl_state, view_state.add_node_target, view_state.add_node_type);
            }
        }
        break;
    }
    case view_action_type_e::NODE_UPDATE: {
        auto& node_action = view_state.node_action;
        switch (node_action.node_desc.type)
        {
        case NodeType_Repeat:
            update_repeat_node_times(bl_state, node_action.node_desc, node_action.action_data.repeat_count);
            break;
        
        default:
            break;
        }
        break;
    }

    case view_action_type_e::REFRESH_WAV_LIST:
        refresh_wav_list(state);
        break;

    case view_action_type_e::BUS_ADD:
        add_output_bus(state);
        break;
    case view_action_type_e::BUS_RENAME:
        rename_bus(bl_state, 
            view_state.bus_edit_state.index,
            view_state.bus_edit_state.name); 
        break;

    case view_action_type_e::BUS_VOLUME_CHANGED: {
        uint8_t index = 0;
        for (auto volume_percents : state->view_state.output_bus_volumes) {
            hlea_set_bus_volume(state->runtime_ctx, index, (float)volume_percents / 100);
            ++index;
        }
        break;
    }

    case view_action_type_e::SOUND_PLAY: {
        auto file_index = view_state.selected_sound_file_index;
        auto full_path_str = state->wav_files[file_index].u8string();
        hlea_play_file(state->runtime_ctx, full_path_str.c_str());
        break;
    }
    case view_action_type_e::SOUND_STOP:
        hlea_stop_file(state->runtime_ctx);
        break;
    case view_action_type_e::RUNTIME_FIRE_EVENT: {
        fire_event(state);
        break;
    }

    case view_action_type_e::NONE:
        break;
    default:
        assert(false && "unhandled action");
        break;
    }

    auto& node_action = view_state.node_action;
    if (node_action.action_remove) {
        if (node_action.parent_node_desc.type == NodeType_None) {
            // root node case, detach from group
            remove_root_node(bl_state, 
                view_state.active_group_index);
        } else {
            remove_node(bl_state, 
                node_action.parent_node_desc, node_action.node_index);
        }
    }
    if (node_action.action_assign_sound) {
        if (node_action.node_desc.type == NodeType_File) {
            auto file_list_index = state->view_state.selected_sound_file_index;
            const auto& filename = state->wav_files_u8_names[file_list_index];
            assign_file_node_file(bl_state, node_action.node_desc, filename);
        }
    }
    if (node_action.action_switch_loop) {
        if (node_action.node_desc.type == NodeType_File) {
            switch_file_node_loop(bl_state, node_action.node_desc);
        }
    }
    if (node_action.action_switch_stream) {
        if (node_action.node_desc.type == NodeType_File) {
            switch_file_node_stream(bl_state, node_action.node_desc);
        }
    }
}

}
}

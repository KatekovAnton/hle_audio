#include "app_view.h"
// #include "cmd_stack.h"
#include "data_types.h"
#include "imgui.h"

namespace ImGuiExt {
bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f);
}

namespace hle_audio {
namespace editor {

struct command_stack_t;

static bool TreeNodeWithRemoveButton(uint32_t node_index, const char* label, bool* remove_pressed) {
    bool expanded = ImGui::TreeNodeEx((void*)(intptr_t)node_index, ImGuiTreeNodeFlags_AllowItemOverlap, label);
    ImGui::SameLine();
    ImGui::PushID(expanded ? -1 - node_index : node_index); // fix id conflict with expanded node and its unexpanded children
    if (ImGui::SmallButton("-")) {
        *remove_pressed = true;
    }
    ImGui::PopID();

    return expanded;
}

static void build_node_tree(const data_state_t& state, const view_state_t& view_state,
                const node_desc_t& parent_node_desc,
                uint32_t node_index, const node_desc_t& node_desc,
                node_action_data_t& out_action) {
    bool add_node = false;
    bool removePressed = false;

    switch (node_desc.type)
    {
    case NodeType_None: {
        ImGui::Text("None");
        ImGui::SameLine();
        if (ImGui::SmallButton("Add...")) {
            // add node popup
            add_node = true;
        }
        break;
    }
    case NodeType_File: {
        auto& file_node = get_file_node(&state, node_desc.id);

        {
            const char* file_name = file_node.filename.c_str(); //"None";
            
            if (TreeNodeWithRemoveButton(node_index, file_name, &removePressed)) {
                ImGui::BeginDisabled(view_state.selected_sound_file_index == invalid_index);
                if (ImGui::SmallButton("Use selected sound file")) {
                    out_action.node_desc = node_desc;
                    out_action.action_assign_sound = true;
                }
                ImGui::EndDisabled();

                bool loop_state = file_node.loop;
                if (ImGui::Checkbox("loop", &loop_state)) {
                    out_action.node_desc = node_desc;
                    out_action.action_switch_loop = true;
                }

                bool stream_state = file_node.stream;
                if (ImGui::Checkbox("stream", &stream_state)) {
                    out_action.node_desc = node_desc;
                    out_action.action_switch_stream = true;
                }

                ImGui::TreePop();
            }
        }
        
        break;
    }
    case NodeType_Random:
    case NodeType_Sequence:
        if (TreeNodeWithRemoveButton(node_index, EnumNameNodeType(node_desc.type), &removePressed)) {
            auto node_ptr = get_child_nodes_ptr(&state, node_desc);
            uint32_t child_index = 0;
            for (auto& child_desc : *node_ptr) {
                build_node_tree(state, view_state, node_desc, child_index++, child_desc, out_action);
            }
            if (ImGui::SmallButton("Add...")) {
                add_node = true;
            }
            ImGui::TreePop();
        }
        break;
    case NodeType_Repeat:
        if (TreeNodeWithRemoveButton(node_index, EnumNameNodeType(node_desc.type), &removePressed)) {
            auto& repeat_node = get_repeat_node(&state, node_desc.id);

            static node_desc_t changing_node = {};
            static uint16_t changing_value;

            uint16_t repeat_count = repeat_node.repeat_count;
            uint16_t* repeat_count_ptr = (node_desc.type == changing_node.type && node_desc.id == changing_node.id) ? &changing_value : &repeat_count;
            if (ImGui::DragScalar("times", ImGuiDataType_U16, repeat_count_ptr)) {
                changing_node = node_desc;
                changing_value = *repeat_count_ptr;
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                changing_node = {};

                out_action.action = view_action_type_e::NODE_UPDATE;
                out_action.node_desc = node_desc;
                out_action.action_data.repeat_count = changing_value;
            }

            if (repeat_node.node.type != NodeType_None) {
                build_node_tree(state, view_state, node_desc, 0, repeat_node.node, out_action);
            } else if (ImGui::SmallButton("Add...")) {
                add_node = true;
            }
            ImGui::TreePop();
        }
        break;
    default:
        ImGui::Text("Default");
        break;
    }

    if (add_node) {
        out_action.node_desc = node_desc;
        out_action.action_add = true;
    }
    if (removePressed) {
        out_action.parent_node_desc = parent_node_desc;
        out_action.node_index = node_index;
        out_action.action_remove = true;
    }
}

namespace ImGui_std {

static int InputTextCallback(ImGuiInputTextCallbackData* data) {
    std::string* str = (std::string*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        // Resize string callback
        // If for some reason we refuse the new length (BufTextLen) and/or capacity (BufSize) we need to set them back to what we want.
        IM_ASSERT(data->Buf == str->c_str());
        str->resize(data->BufTextLen);
        data->Buf = (char*)str->c_str();
    }
    return 0;
}

bool InputText(const char* label, std::string* str, ImGuiInputTextFlags flags) {
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    return ImGui::InputText(label, (char*)str->c_str(), str->capacity() + 1, flags, InputTextCallback, str);
}

}

static view_action_type_e process_view_menu(const view_state_t& view_state) {
    static bool show_app_metrics = false;
    static bool show_demo_window = false;

    view_action_type_e action = view_action_type_e::NONE;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            // if (ImGui::MenuItem("New", "CTRL+N")) {}
            // if (ImGui::MenuItem("Open...", "CTRL+O")) {}
            if (ImGui::MenuItem("Save", "CTRL+S", false, view_state.has_save)) {
                action = view_action_type_e::SAVE;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "CTRL+Z", false, view_state.has_undo)) {
                action = view_action_type_e::UNDO;
            }
            if (ImGui::MenuItem("Redo", "CTRL+Y", false, view_state.has_redo)) {
                action = view_action_type_e::REDO;
            }
            // ImGui::Separator();
            // if (ImGui::MenuItem("Cut", "CTRL+X")) {}
            // if (ImGui::MenuItem("Copy", "CTRL+C")) {}
            // if (ImGui::MenuItem("Paste", "CTRL+V")) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Show UI metrics")) show_app_metrics = true;
            if (ImGui::MenuItem("Show demos")) {
                show_demo_window = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    if (show_app_metrics) {
        ImGui::ShowMetricsWindow(&show_app_metrics);
    }

    return action;
}

typedef const char* (*get_text_at_index_cb)(const void* ud, int index);

static void ClippedListWithAddRemoveButtons(size_t elem_count, float scale,
                size_t selected_index, const void* ud, get_text_at_index_cb get_text_at_index,
                size_t* new_selected_index, bool* add_pressed, bool* remove_pressed, bool* double_clicked = nullptr) {
    assert(new_selected_index);
    assert(add_pressed);
    assert(remove_pressed);

    ImGuiListClipper clipper;
    clipper.Begin((int)elem_count);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            ImGui::PushID(i);
            auto name = get_text_at_index(ud, i);
            if (ImGui::Selectable(name, selected_index == i, ImGuiSelectableFlags_AllowDoubleClick)) {
                *new_selected_index = i;
                if (ImGui::IsMouseDoubleClicked(0) && double_clicked) {
                    * double_clicked = true;
                }
            }

            if (selected_index == i) {
                ImGui::SetItemAllowOverlap();

                auto content_width_with_scroll = ImGui::GetContentRegionMax().x - 2 * ImGui::GetWindowContentRegionMin().x;
                ImGui::SameLine(content_width_with_scroll - 30 * scale);
                if (ImGui::SmallButton("-")) {
                    *remove_pressed = true;
                }
                ImGui::SameLine(content_width_with_scroll - 15 * scale);
                if (ImGui::SmallButton("+")) {
                    *add_pressed = true;
                }
            }
            ImGui::PopID();
        }
    }
}

static void build_selected_group_view(view_state_t& mut_view_state, const data_state_t& data_state,
                view_action_type_e& action, node_action_data_t& node_action) {
    auto& group_state = mut_view_state.selected_group_state;

    ImGui_std::InputText("name", &group_state.name, ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
    }
    ImGui::SliderFloat("volume", &group_state.volume, 0.0f, 1.0f);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
    }

    const char* time_sec_format = "%.3f";
    const float f32_zero = 0.0f;
    ImGui::DragScalar("cross fade time", ImGuiDataType_Float, 
            &group_state.cross_fade_time, 
            0.01f,  &f32_zero, nullptr,
            time_sec_format);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
    }

    int current_index = group_state.output_bus_index;
    auto getter = [](void* data, int n, const char** out_str) {
        auto buses = (decltype(&data_state.output_buses))data;
        *out_str = buses->at(n).name.c_str();
        return true;
    };
    void* data = (void*)&data_state.output_buses;
    if (ImGui::Combo("output bus", &current_index, 
            getter, data, (int)data_state.output_buses.size())) {
    // if (current_index != action_ptr->type) {
        group_state.output_bus_index = current_index;
        action = view_action_type_e::APPLY_SELECTED_GROUP_UPDATE;
    }

    ImGui::Text("Node tree:");
    build_node_tree(data_state, mut_view_state, invalid_node_desc, 0, group_state.node, node_action);

    ImGui::Separator();
    if (ImGui::Button("<<< Filter events")) {
        mut_view_state.event_filter_group_index = mut_view_state.action_group_index;
        mut_view_state.groups_size_on_event_filter_group = data_state.groups.size();
        mut_view_state.select_events_tab = true;
        action = view_action_type_e::EVENT_FILTER;
    }
}

view_action_type_e build_view(view_state_t& mut_view_state, const data_state_t& data_state) {
    view_action_type_e action = process_view_menu(mut_view_state);

    float root_pane_width_max = ImGui::GetContentRegionAvail().x - mut_view_state.root_pane_width_scaled;
    ImGuiExt::Splitter(true, 4.0f, 
            &mut_view_state.root_pane_width_scaled, &root_pane_width_max, 
            50 * mut_view_state.scale, 8);

    static bool focus_new_group = false;

    bool do_focus_new_group = focus_new_group;
    focus_new_group = false;
    
    const auto active_group_index = mut_view_state.active_group_index;
    mut_view_state.action_group_index = active_group_index;
    {
        auto padding_x = ImGui::GetStyle().WindowPadding.x;

        ImGui::BeginChild("root_pane", ImVec2(mut_view_state.root_pane_width_scaled - padding_x / 2 + 2, 0));
        if (ImGui::BeginTabBar("root_objects", ImGuiTabBarFlags_None)) {

            //
            // sound groups
            //
            if (ImGui::BeginTabItem("Groups")) {
                bool scroll_to_end = false;
                if (ImGui::Button("+")) {
                    action = view_action_type_e::GROUP_ADD;
                    mut_view_state.action_group_index = data_state.groups.size() - 1;
                }

                ImGui::Separator();

                ImGui::BeginChild("Groups_list");

                bool add_pressed = false;
                bool remove_pressed = false;
                using groups_type_t = decltype(data_state.groups);
                auto groups_size = data_state.groups.size();
                ClippedListWithAddRemoveButtons(
                    groups_size, 
                    mut_view_state.scale, mut_view_state.active_group_index, 
                    &data_state.groups, [](const void* ud, int index) {
                        auto elems_ptr = (const groups_type_t*)ud;
                        return elems_ptr->at(index).name.c_str();
                    },
                    &mut_view_state.active_group_index,
                    &add_pressed, &remove_pressed);
                // scroll to end when new group is added
                if (do_focus_new_group && active_group_index == groups_size - 1) {
                    ImGui::SetScrollHereY();
                }
                if (add_pressed) {
                    action = view_action_type_e::GROUP_ADD;
                }
                if (remove_pressed) {
                    action = view_action_type_e::GROUP_REMOVE;
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }
            ImGuiTabItemFlags events_tab_flags = 0;
            if (mut_view_state.select_events_tab) {
                mut_view_state.select_events_tab = false;
                events_tab_flags = ImGuiTabItemFlags_SetSelected;
            }
            if (ImGui::BeginTabItem("Events", nullptr, events_tab_flags)) {
                if (ImGui::InputTextWithHint("Filter", "enter text here", 
                        mut_view_state.event_filter_str, IM_ARRAYSIZE(mut_view_state.event_filter_str))) {
                    action = view_action_type_e::EVENT_FILTER;
                }

                // group filter
                if (mut_view_state.event_filter_group_index != invalid_index) {
                    auto& group = data_state.groups[mut_view_state.event_filter_group_index];
                    
                    if (ImGui::SmallButton("x")) {
                        mut_view_state.event_filter_group_index = invalid_index;
                        action = view_action_type_e::EVENT_FILTER;
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s", group.name.c_str());
                }

                ImGui::Separator();
                
                ImGui::BeginChild("Events_list");
                bool add_pressed = false;
                bool remove_pressed = false;
                bool double_clicked = false;
            
                struct clipper_ctx {
                    view_state_t& mut_view_state;
                    const data_state_t& data_state;
                };
                clipper_ctx ctx = {mut_view_state, data_state};
                ClippedListWithAddRemoveButtons(
                    (int)mut_view_state.filtered_event_indices.size(), 
                    mut_view_state.scale, mut_view_state.event_list_index, 
                    &ctx, [](const void* ud, int index) {
                        auto ctx_ptr = (clipper_ctx*)ud;
                        auto event_index = ctx_ptr->mut_view_state.filtered_event_indices[index];
                        return ctx_ptr->data_state.events[event_index].name.c_str();
                    },
                    &mut_view_state.event_list_index, 
                    &add_pressed, &remove_pressed, &double_clicked);
                
                if (add_pressed) {
                    action = view_action_type_e::EVENT_ADD;
                }
                if (remove_pressed) {
                    action = view_action_type_e::EVENT_REMOVE;
                }
                if (double_clicked) {
                    action = view_action_type_e::RUNTIME_FIRE_EVENT;
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }

    node_action_data_t node_action = {};

    auto& style = ImGui::GetStyle();
    const float wav_list_width = 200 * mut_view_state.scale;

    ImGui::SameLine();
    ImGui::BeginChild("Properties pane", ImVec2(-wav_list_width - style.WindowPadding.x, 0), true);

    if (active_group_index != invalid_index) {
        if (ImGui::CollapsingHeader("Group Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (do_focus_new_group) {
                ImGui::SetKeyboardFocusHere();
            }

            build_selected_group_view(mut_view_state, data_state, action, node_action);
        }
    }
    mut_view_state.node_action = node_action;
    if (node_action.action != view_action_type_e::NONE) {
        action = node_action.action;
    }

    const auto active_event_index = mut_view_state.active_event_index;
    // auto new_action_index = mut_view_state.active_action_index;
    if (active_event_index != invalid_index) {
        if (ImGui::CollapsingHeader("Event Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& event_state = mut_view_state.event_state;
            ImGui::InputText("name##event_name", event_state.name, IM_ARRAYSIZE(event_state.name), ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                action = view_action_type_e::EVENT_UPDATE;
            }

            ImGui::Text("Actions:");
            if (ImGui::BeginTable("actions", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100 * mut_view_state.scale);
                ImGui::TableSetupColumn("Target group", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Fade time", ImGuiTableColumnFlags_WidthFixed, 50 * mut_view_state.scale);
                ImGui::TableHeadersRow();

                size_t action_index = 0;
                auto& event = data_state.events[active_event_index];
                for (auto& action_ptr : event.actions) {
                    ImGui::TableNextRow(0, 20 * mut_view_state.scale);
                    ImGui::PushID((void*)(uintptr_t)action_index);

                    bool action_selected = action_index == mut_view_state.active_action_index;
                    auto& active_action = mut_view_state.active_action;

                    // type
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);

                    ImGui::BeginDisabled(!action_selected);
                    int current_index = action_ptr->type;
                    ImGui::Combo("##type", &current_index, EnumNamesActionType(), ActionType_MAX + 1);
                    if (current_index != action_ptr->type) {
                        active_action.type = (ActionType)current_index;

                        // remove when none is selected
                        if (active_action.type == ActionType_none) {
                            action = view_action_type_e::EVENT_REMOVE_ACTION;
                        } else {
                            action = view_action_type_e::EVENT_UPDATE_ACTION;
                        }
                    }
                    ImGui::EndDisabled();
                    
                    //
                    // target group
                    //
                    ImGui::TableNextColumn();
                    
                    const char* target_label = "all groups";
                    if (!is_action_target_all(*action_ptr)) {
                        auto& group = data_state.groups[action_ptr->target_group_index];
                        target_label = group.name.c_str();
                    }
                
                    if (ImGui::Selectable(target_label, false,//action_selected, 
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap)) {
                        mut_view_state.active_action_index = action_index;
                    }
                    if (action_selected && 
                            !is_action_target_all(*action_ptr) &&
                            active_group_index != invalid_index) {

                        ImGui::SameLine();
                        if (ImGui::Button("<<<")) {
                            active_action.target_group_index = active_group_index;
                            action = view_action_type_e::EVENT_UPDATE_ACTION;
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("Assign active group");
                        }
                    }


                    // fade time
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::BeginDisabled(!action_selected);

                    const char* time_sec_format = "%.3f";

                    float fade_time_read = action_ptr->fade_time;
                    float* value_ptr = action_selected ? &active_action.fade_time : &fade_time_read;

                    const float f32_zero = 0.0f;
                    ImGui::DragScalar("##fade_time", ImGuiDataType_Float, 
                            value_ptr, 
                            0.01f,  &f32_zero, nullptr,
                            time_sec_format);
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        action = view_action_type_e::EVENT_UPDATE_ACTION;
                    }

                    ImGui::EndDisabled();

                    ImGui::PopID();
                    ++action_index;
                }

                ImGui::EndTable();
            }

            if (ImGui::Button("Add")) {
                action = view_action_type_e::EVENT_APPEND_ACTION;
            }

            ImGui::Separator();
            if (ImGui::Button("Fire")) {
                action = view_action_type_e::RUNTIME_FIRE_EVENT;
            }
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // bus edit data
    bool open_bus_popup = false;
    ImGui::BeginChild("right_pane", ImVec2(wav_list_width, 0));
    if (ImGui::CollapsingHeader("Output buses", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        ImGui::Text("Volumes:");
        size_t index = 0;
        for (auto& bus : data_state.output_buses) {
            ImGui::PushID((void*)(uintptr_t)index);

            if (ImGui::Button("..")) {
                open_bus_popup = true;
                mut_view_state.bus_edit_state.index = index;
                snprintf(mut_view_state.bus_edit_state.name, sizeof(mut_view_state.bus_edit_state.name), 
                    "%s", bus.name.c_str());
                
            }
            ImGui::SameLine();
            int* v_ptr = &mut_view_state.output_bus_volumes[index];
            if (ImGui::SliderInt(bus.name.c_str(), v_ptr, 0, 100)) {
                action = view_action_type_e::BUS_VOLUME_CHANGED;
            }

            ImGui::PopID();
            ++index;
        }

        if (ImGui::SmallButton("Add bus")) {
            action = view_action_type_e::BUS_ADD;
        }
        ImGui::Separator();
    }

    //
    // wav file list
    //
    {
        auto& file_list = *mut_view_state.wav_files_u8_names_ptr;

        ImGui::BeginGroup();
        
        ImGui::Text("Sound files (%d):", (int)file_list.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            action = view_action_type_e::REFRESH_WAV_LIST;
        }

        ImGui::BeginChild("Wavs", ImVec2(wav_list_width, 0), true);
        ImGuiListClipper clipper;
        clipper.Begin((int)file_list.size());
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                auto& filename = file_list[i];
                if (ImGui::Selectable(filename.c_str(), mut_view_state.selected_sound_file_index == i))
                    mut_view_state.selected_sound_file_index = i;
                if (mut_view_state.selected_sound_file_index == i) {
                    ImGui::SetItemAllowOverlap();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Play")) {
                        action = view_action_type_e::SOUND_PLAY;
                    }
                    if (mut_view_state.has_wav_playing) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Stop")) {
                            action = view_action_type_e::SOUND_STOP;
                        }   
                    }
                }
            }
        ImGui::EndChild();

        ImGui::EndGroup();
    }   

    ImGui::EndChild(); // right pane

    if (node_action.action_add) {
        mut_view_state.add_node_target = node_action.node_desc;
        ImGui::OpenPopup("create_node_popup");
    }
    mut_view_state.add_node_type = NodeType_None;
    if (ImGui::BeginPopup("create_node_popup")) {
        for (int i = 1; i <= NodeType_MAX; i++) {
            if (ImGui::Selectable(EnumNamesNodeType()[i])) {
                mut_view_state.add_node_type = EnumValuesNodeType()[i];
                action = view_action_type_e::NODE_ADD;
            }
        }
        ImGui::EndPopup();
    }

    if (open_bus_popup) {
        ImGui::OpenPopup("show_bus_popup");
    }    
    if (ImGui::BeginPopup("show_bus_popup")) {
        ImGui::InputText("name", 
                mut_view_state.bus_edit_state.name, IM_ARRAYSIZE(mut_view_state.bus_edit_state.name), 
                ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            action = view_action_type_e::BUS_RENAME;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }

    // hotkyes
    // todo: unify actions with main menus
    if (!ImGui::IsAnyItemActive()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            action = view_action_type_e::SAVE;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            action = view_action_type_e::UNDO;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            action = view_action_type_e::REDO;
        }
    }

    switch (action)
    {
    case view_action_type_e::GROUP_ADD:
        focus_new_group = true;
        break;
    }

    return action;
}

}
}
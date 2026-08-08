/*
 * Copyright (C) 2026 Carlos Lopez
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#define DEBUG_LEVEL_0

#include <windows.h>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>
#include <optional>

#include <embed/shaders.h>

#include "../../mods/shader.hpp"
#include "../../mods/swapchain.hpp"
#include "../../utils/settings.hpp"
#include "./shared.h"

namespace {

ShaderInjectData shader_injection;

renodx::mods::shader::CustomShaders custom_shaders = {
    CustomShaderEntry(0xE363E5C8),
    CustomShaderEntry(0x73F01A45),
    CustomSwapchainShader(0x20133A8B),
};

renodx::utils::settings::Setting* output_mode_setting = nullptr;
renodx::utils::settings::Setting* peak_brightness_setting = nullptr;
renodx::utils::settings::Setting* game_brightness_setting = nullptr;
renodx::utils::settings::Setting* ui_brightness_setting = nullptr;

std::optional<reshade::api::color_space> current_color_space;
std::optional<reshade::api::color_space> pending_color_space;

bool IsHDROutput() {
  return output_mode_setting != nullptr && output_mode_setting->GetValue() != 0.f;
}

void SyncOutput() {
  const bool is_hdr_output = IsHDROutput();
  const bool is_hdr10 =
      renodx::mods::swapchain::target_format == reshade::api::format::r10g10b10a2_unorm;
  const auto color_space = is_hdr_output
                               ? (is_hdr10
                                      ? reshade::api::color_space::hdr10_st2084
                                      : reshade::api::color_space::extended_srgb_linear)
                               : (is_hdr10
                                      ? reshade::api::color_space::srgb_nonlinear
                                      : reshade::api::color_space::extended_srgb_linear);

  shader_injection.swap_chain_output_preset =
      is_hdr_output ? (is_hdr10 ? 1.f : 2.f) : (is_hdr10 ? 0.f : 2.f);
  shader_injection.peak_white_nits = is_hdr_output ? peak_brightness_setting->GetValue() : 1.f;
  shader_injection.diffuse_white_nits = is_hdr_output ? game_brightness_setting->GetValue() : 1.f;
  shader_injection.graphics_white_nits = is_hdr_output ? ui_brightness_setting->GetValue() : 1.f;

  if (!current_color_space.has_value() || current_color_space.value() != color_space) {
    pending_color_space = color_space;
  }
}

renodx::utils::settings::Settings settings = {
    output_mode_setting = new renodx::utils::settings::Setting{
        .key = "OutputMode",
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 0.f,
        .can_reset = false,
        .label = "Output Mode",
        .section = "Output",
        .labels = {"SDR", "HDR"},
        .on_change_value = [](float, float) { SyncOutput(); },
    },
    new renodx::utils::settings::Setting{
        .key = "ToneMapType",
        .binding = &shader_injection.tone_map_type,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "Tone Mapper",
        .section = "Tone Mapping",
        .labels = {"Vanilla", "RenoDRT"},
        .is_enabled = &IsHDROutput,
        .parse = [](float value) { return value * 3.f; },
    },
    peak_brightness_setting = new renodx::utils::settings::Setting{
        .key = "ToneMapPeakNits",
        .binding = &shader_injection.peak_white_nits,
        .default_value = 1000.f,
        .can_reset = false,
        .label = "Peak Brightness",
        .section = "Output",
        .tooltip = "Sets the display peak brightness in nits.",
        .min = 48.f,
        .max = 4000.f,
        .is_enabled = &IsHDROutput,
    },
    game_brightness_setting = new renodx::utils::settings::Setting{
        .key = "ToneMapGameNits",
        .binding = &shader_injection.diffuse_white_nits,
        .default_value = 203.f,
        .label = "Game Brightness",
        .section = "Output",
        .tooltip = "Sets the scene diffuse white brightness in nits.",
        .min = 48.f,
        .max = 500.f,
        .is_enabled = &IsHDROutput,
    },
    ui_brightness_setting = new renodx::utils::settings::Setting{
        .key = "ToneMapUINits",
        .binding = &shader_injection.graphics_white_nits,
        .default_value = 203.f,
        .label = "UI Brightness",
        .section = "Output",
        .tooltip = "Sets the UI and HUD brightness in nits.",
        .min = 48.f,
        .max = 500.f,
        .is_enabled = &IsHDROutput,
    },
    new renodx::utils::settings::Setting{
        .key = "GammaCorrection",
        .binding = &shader_injection.gamma_correction,
        .value_type = renodx::utils::settings::SettingValueType::INTEGER,
        .default_value = 1.f,
        .label = "SDR EOTF Emulation",
        .section = "Output",
        .labels = {"None", "2.2", "BT.1886"},
        .is_enabled = &IsHDROutput,
    },
};

void OnPresetOff() {
  renodx::utils::settings::UpdateSettings({
      {"OutputMode", 0.f},
      {"ToneMapType", 0.f},
      {"GammaCorrection", 0.f},
  });
  SyncOutput();
}

void OnPresent(reshade::api::command_queue*,
               reshade::api::swapchain* swapchain,
               const reshade::api::rect*,
               const reshade::api::rect*,
               uint32_t,
               const reshade::api::rect*) {
  SyncOutput();
  if (pending_color_space.has_value()) {
    renodx::utils::swapchain::ChangeColorSpace(swapchain, pending_color_space.value());
    current_color_space = pending_color_space;
    pending_color_space = std::nullopt;
  }
}

}  // namespace

extern "C" __declspec(dllexport) constexpr const char* NAME = "RenoDX";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION = "RenoDX for Cities: Skylines II";

BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID) {
  switch (fdw_reason) {
    case DLL_PROCESS_ATTACH:
      if (!reshade::register_addon(h_module)) return FALSE;

      renodx::mods::shader::force_pipeline_cloning = true;
      renodx::mods::shader::expected_constant_buffer_space = 50;
      renodx::mods::shader::expected_constant_buffer_index = 13;
      renodx::mods::shader::allow_multiple_push_constants = true;

      renodx::mods::swapchain::SetUseHDR10(true);
      renodx::mods::swapchain::set_color_space = false;
      renodx::mods::swapchain::swap_chain_upgrade_targets.push_back({
          .old_format = reshade::api::format::r8g8b8a8_typeless,
          .new_format = reshade::api::format::r16g16b16a16_float,
          .dimensions = {
              .width = renodx::utils::resource::ResourceUpgradeInfo::BACK_BUFFER,
              .height = renodx::utils::resource::ResourceUpgradeInfo::BACK_BUFFER,
              .depth = 1,
          },
          .usage_include = reshade::api::resource_usage::render_target,
          .usage_exclude = reshade::api::resource_usage::unordered_access,
      });

      renodx::utils::settings::Use(fdw_reason, &settings, &OnPresetOff);
      SyncOutput();
      reshade::register_event<reshade::addon_event::present>(OnPresent);
      break;
    case DLL_PROCESS_DETACH:
      reshade::unregister_event<reshade::addon_event::present>(OnPresent);
      renodx::utils::settings::Use(fdw_reason, &settings, &OnPresetOff);
      renodx::mods::swapchain::set_color_space = true;
      current_color_space = std::nullopt;
      pending_color_space = std::nullopt;
      reshade::unregister_addon(h_module);
      break;
  }

  renodx::mods::swapchain::Use(fdw_reason, &shader_injection);
  renodx::mods::shader::Use(fdw_reason, custom_shaders, &shader_injection);

  return TRUE;
}

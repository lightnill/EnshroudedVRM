# Pinned Dear ImGui sources

Unmodified selected files from official ocornut/imgui v1.91.9b, revision
f5befd2d29e66809cd1110a152e375a7f1981f06. SOURCE-PINS.json records exact bytes.
Core plus Vulkan renderer are used with the project-owned bounded Win32 adapter.
No upstream platform/backend file is modified. Compile upstream code separately
without the project Werror policy. Disable Vulkan prototypes through build flags.

LICENSE.txt and embedded header/font notices remain intact. BINARY-NOTICES.txt
collects the MIT notices for Dear ImGui, embedded ProggyClean.ttf (Tristan Grimmer)
and the selected MIT alternative for embedded stb headers (Sean Barrett).
No demo application, executable binary or build tool is included.

The same pinned repository supplies Roboto-Medium.ttf, by Christian Robertson,
Apache-2.0. It is embedded as immutable font bytes in mod_menu_font.generated.inl;
no font file is read at runtime. The Apache license comes from apache.org and its
source override/hash are explicit in SOURCE-PINS.json. Keep all binary notices.

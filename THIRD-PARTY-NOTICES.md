# Third-party notices

WhisperVTT is built on the following components. Their license texts are in
[`LICENSES/`](LICENSES) and ship in every release package.

| Component | How it is used | License |
|---|---|---|
| [whisper.cpp / ggml](https://github.com/ggml-org/whisper.cpp) | Speech recognition engine and its CPU and Vulkan backends | MIT — [`LICENSES/whisper.cpp-MIT.txt`](LICENSES/whisper.cpp-MIT.txt) |
| [Silero VAD](https://github.com/snakers4/silero-vad) v6.2.0 | Voice-activity model embedded in the executable (ggml conversion from [ggml-org/whisper-vad](https://huggingface.co/ggml-org/whisper-vad)) | MIT — [`LICENSES/silero-vad-MIT.txt`](LICENSES/silero-vad-MIT.txt) |
| [Qt 6](https://www.qt.io/) | GUI framework, dynamically linked | LGPL-3.0 — [`LICENSES/LGPL-3.0.txt`](LICENSES/LGPL-3.0.txt), [`LICENSES/GPL-3.0.txt`](LICENSES/GPL-3.0.txt) |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Compiled into the Vulkan backend (`ggml-vulkan.dll`). The Vulkan runtime itself comes with the graphics driver and is not distributed here. | Apache-2.0 OR MIT — [`LICENSES/Vulkan-Headers-MIT.txt`](LICENSES/Vulkan-Headers-MIT.txt) |
| Microsoft Visual C++ runtime | C++ and OpenMP runtime libraries | [Visual Studio redistribution terms](https://learn.microsoft.com/visualstudio/releases/2022/redistribution) |
| [OpenAI Whisper](https://github.com/openai/whisper) models | Downloaded separately by the user; not distributed here | MIT |

Qt is used under the LGPL-3.0: it is linked dynamically, and its DLLs can be
replaced with any compatible build. Qt source code is available from
<https://download.qt.io/>.

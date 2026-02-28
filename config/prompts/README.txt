MixAgent System Prompts for Local LLM Models
=============================================

These prompt files contain expert live sound engineering knowledge that local
LLM models (Ollama, llama.cpp, etc.) can use as system prompts to make better
mix decisions.

FILES:
  mix_engineer_core.txt     — Core system prompt with rules, safety limits,
                              response format, and general mix hierarchy.
                              This is the minimum prompt any model needs.

  mix_balance_reference.txt — Detailed reference guide covering gain staging,
                              relative level targets, HPF starting points,
                              EQ frequency guide, dynamics processing, stereo
                              field, and decision framework.

  mix_troubleshooting.txt   — Issue-specific troubleshooting guide. Maps DSP
                              analysis issues (clipping, boomy, harsh, thin,
                              masking, muddy, no headroom) to specific fixes.

  genre_rock.txt            — Rock/pop-rock specific mix guidance.
  genre_jazz.txt            — Jazz/acoustic jazz mix guidance.
  genre_worship.txt         — Worship/contemporary church mix guidance.
  genre_edm.txt             — EDM/electronic/DJ set mix guidance.
  genre_acoustic.txt        — Acoustic/singer-songwriter/folk mix guidance.

USAGE WITH OLLAMA:
  Create a Modelfile that includes the relevant prompts:

    FROM llama3:8b
    SYSTEM """
    <paste mix_engineer_core.txt here>

    <paste genre_rock.txt here if mixing rock>

    <paste mix_troubleshooting.txt here for issue-aware decisions>
    """
    PARAMETER temperature 0.3
    PARAMETER num_predict 1024

  Then: ollama create mixagent-rock -f Modelfile

USAGE WITH MIXAGENT:
  MixAgent automatically includes the core system prompt when calling the LLM.
  The genre presets are injected as JSON data in the mix state context.
  These files are provided as standalone references for custom model setups.

TIPS FOR LOCAL MODELS:
  - Smaller models (7B-13B) work best with mix_engineer_core.txt only.
    Adding too much context can confuse them.
  - Medium models (13B-34B) handle core + one genre prompt well.
  - Large models (70B+) can handle all prompts concatenated.
  - Always include mix_engineer_core.txt — it has the response format.
  - The genre prompt should match the show type.
  - mix_troubleshooting.txt is most useful when your DSP analysis is active
    (audio_channels > 0 in config), as the issues array provides the data
    the troubleshooting guide references.

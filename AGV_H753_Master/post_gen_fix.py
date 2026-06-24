"""
Run after every CubeMX code regeneration to fix known issues.
Usage: python post_gen_fix.py
"""
import re
from pathlib import Path

BASE = Path(__file__).parent
fixes = 0

# ---- 1. app_x-cube-ai.c: remove static from model handles ----
ai_c = BASE / "X-CUBE-AI/App/app_x-cube-ai.c"
if ai_c.exists():
    txt = ai_c.read_text(encoding='utf-8')

    # Remove static from kws, ai_input, ai_output
    txt, n = re.subn(r'static ai_handle kws', 'ai_handle kws', txt)
    fixes += n
    txt, n = re.subn(r'static ai_buffer\* ai_input;', 'ai_buffer* ai_input;', txt)
    fixes += n
    txt, n = re.subn(r'static ai_buffer\* ai_output;', 'ai_buffer* ai_output;', txt)
    fixes += n

    # Disable do {} while(1) hang
    txt, n = re.subn(r'  do \{\} while \(1\);',
                     '  /* do {} while (1); -- disabled */', txt)
    fixes += n

    # Simplify MX_X_CUBE_AI_Process (keep empty, KWS calls ai_kws_run directly)
    old_proc = r'(void MX_X_CUBE_AI_Process\(void\)\s*\{)\s*/\* USER CODE BEGIN 6 \*/\s*.*?/\* USER CODE END 6 \*/\s*\}'
    new_proc = r'\1\n    /* USER CODE BEGIN 6 */\n  /* KWS pipeline calls ai_kws_run() directly from kws.c */\n    /* USER CODE END 6 */\n}'
    txt, n = re.subn(old_proc, new_proc, txt, flags=re.DOTALL)
    fixes += n

    # Remove printf from MX_X_CUBE_AI_Init
    txt = txt.replace('  printf("\\r\\nTEMPLATE - initialization\\r\\n");\n\n  ai_boostrap',
                      '  ai_boostrap')
    # Also handle case without double newline
    txt, n = re.subn(r'printf\(".*TEMPLATE.*\\r\\n"\);\s*ai_boostrap', 'ai_boostrap', txt)
    fixes += n

    ai_c.write_text(txt, encoding='utf-8')
    print(f"  [OK] app_x-cube-ai.c: {n} fix(es)")

# ---- 2. main.c: ensure PLL2_Enable in SysInit ----
main_c = BASE / "Core/Src/main.c"
if main_c.exists():
    txt = main_c.read_text(encoding='utf-8')

    # Check if PLL2_Enable already exists
    if '_pll2_done' not in txt:
        sysinit = (
            '  /* USER CODE BEGIN SysInit */\n'
            '  /* PLL2 must be enabled BEFORE DFSDM init (needs audio clock).\n'
            '     SAI1 MspInit also configures PLL2 but runs later; the static guard\n'
            '     prevents double-config, making the MspInit call a safe no-op. */\n'
            '  {\n'
            '    static uint8_t _pll2_done = 0;\n'
            '    if (!_pll2_done) {\n'
            '      RCC_PeriphCLKInitTypeDef p = {0};\n'
            '      p.PeriphClockSelection = RCC_PERIPHCLK_SAI1;\n'
            '      p.PLL2.PLL2M = 8;\n'
            '      p.PLL2.PLL2N = 172;\n'
            '      p.PLL2.PLL2P = 2;\n'
            '      p.PLL2.PLL2Q = 4;\n'
            '      p.PLL2.PLL2R = 2;\n'
            '      p.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_0;\n'
            '      p.PLL2.PLL2VCOSEL = RCC_PLL2VCOMEDIUM;\n'
            '      p.PLL2.PLL2FRACN = 0;\n'
            '      p.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL2;\n'
            '      HAL_RCCEx_PeriphCLKConfig(&p);\n'
            '      _pll2_done = 1;\n'
            '    }\n'
            '  }\n'
            '  /* USER CODE END SysInit */'
        )
        txt = txt.replace(
            '  /* USER CODE BEGIN SysInit */\n\n  /* USER CODE END SysInit */',
            sysinit
        )
        fixes += 1
        print(f"  [OK] main.c: PLL2_Enable added to SysInit")
    else:
        print(f"  [OK] main.c: PLL2_Enable already present")

    main_c.write_text(txt, encoding='utf-8')

# ---- 3. .ioc: ensure HSE is Crystal (not BYPASS) ----
ioc = BASE / "AGV_H753_Master.ioc"
if ioc.exists():
    txt = ioc.read_text(encoding='utf-8')
    # Check for BYPASS
    if 'HSE_BYPASS' in txt or 'BYPASS_CLOCK_SOURCE' in txt:
        print(f"  [WARN] .ioc: HSE may be set to BYPASS - manually check in CubeMX!")
    else:
        print(f"  [OK] .ioc: HSE appears to be Crystal (no BYPASS)")

print(f"\nDone. {fixes} fix(es) applied.")

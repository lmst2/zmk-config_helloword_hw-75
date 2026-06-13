/* Auto-generated from config/hw75_keyboard.keymap. */
export const keyboardConfig = {
  "sourcePath": "config/hw75_keyboard.keymap",
  "sourceText": "/*\r\n * Copyright (c) 2022 The ZMK Contributors\r\n * SPDX-License-Identifier: MIT\r\n */\r\n\r\n#include <behaviors.dtsi>\n#include \"dts/behaviors/touchbar_mode.dtsi\"\n#include \"dts/behaviors/function_slot.dtsi\"\n#include <dt-bindings/zmk/keys.h>\n#include <dt-bindings/zmk/rgb.h>\n\r\n#define BASE 0\r\n#define FN 1\r\n#define TOUCH 2\r\n\r\n&sl {\r\n  release-after-ms = <500>;\r\n};\r\n\r\n/ {\r\n\tkeymap {\r\n\t\tcompatible = \"zmk,keymap\";\r\n\t\tbase {\r\n\t\t\tlabel = \"BASE\";\r\n\t\t\tbindings = <\r\n\t\t\t\t&kp ESC             &kp F1    &kp F2  &kp F3  &kp F4  &kp F5    &kp F6  &kp F7  &kp F8    &kp F9    &kp F10   &kp F11   &kp F12   &fn_slot 0\n\t\t\t\t&kp GRAVE &kp N1    &kp N2    &kp N3  &kp N4  &kp N5  &kp N6    &kp N7  &kp N8  &kp N9    &kp N0    &kp MINUS &kp EQUAL &kp BSPC  &fn_slot 1\n\t\t\t\t&kp TAB   &kp Q     &kp W     &kp E   &kp R   &kp T   &kp Y     &kp U   &kp I   &kp O     &kp P     &kp LBKT  &kp RBKT  &kp BSLH  &fn_slot 2\n\t\t\t\t&kp CLCK  &kp A     &kp S     &kp D   &kp F   &kp G   &kp H     &kp J   &kp K   &kp L     &kp SEMI  &kp SQT             &kp RET   &fn_slot 3\n\t\t\t\t&kp LSHFT           &kp Z     &kp X   &kp C   &kp V   &kp B     &kp N   &kp M   &kp COMMA &kp DOT   &kp FSLH  &kp RSHFT &kp UP    &fn_slot 4\n\t\t\t\t&kp LCTRL &kp LGUI  &kp LALT                          &kp SPACE                 &kp RALT  &mo FN    &kp RCTRL &kp LEFT  &kp DOWN  &kp RIGHT\n\t\t\t\t&none &none &none &none &none &none\n\t\t\t>;\n\t\t};\n\r\n\t\tfn {\r\n\t\t\tlabel = \"FN\";\r\n\t\t\tbindings = <\r\n\t\t\t\t&trans                          &kp C_BRI_DEC   &kp C_BRI_INC   &none           &none           &none   &none   &kp C_PREV  &kp C_PP  &kp C_NEXT  &kp C_MUTE  &kp C_VOL_DN  &kp C_VOL_UP  &fn_slot 0\n\t\t\t\t&trans          &trans          &trans          &trans          &trans          &trans          &trans  &trans  &trans      &trans    &trans      &trans      &trans        &trans        &fn_slot 1\n\t\t\t\t&rgb_ug RGB_TOG &rgb_ug RGB_EFF &rgb_ug RGB_BRI &rgb_ug RGB_HUI &rgb_ug RGB_SAI &rgb_ug RGB_SPI &trans  &trans  &trans      &trans    &trans      &trans      &trans        &trans        &fn_slot 2\n\t\t\t\t&trans          &rgb_ug RGB_EFR &rgb_ug RGB_BRD &rgb_ug RGB_HUD &rgb_ug RGB_SAD &rgb_ug RGB_SPD &trans  &trans  &trans      &trans    &trans      &trans                    &trans        &fn_slot 3\n\t\t\t\t&trans                          &trans          &trans          &trans          &trans          &trans  &trans  &trans      &trans    &trans      &trans      &trans        &trans        &fn_slot 4\n\t\t\t\t&trans          &trans          &trans                                                          &trans                      &trans    &trans      &tb_mode    &trans        &trans        &trans\n\t\t\t\t&trans  &trans  &trans  &trans  &trans  &trans\n\t\t\t>;\n\t\t};\n\r\n\t\ttouch {\r\n\t\t\tlabel = \"Touch\";\r\n\t\t\tbindings = <\r\n\t\t\t\t&trans          &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &fn_slot 0\n\t\t\t\t&trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &fn_slot 1\n\t\t\t\t&trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &fn_slot 2\n\t\t\t\t&trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans          &trans  &fn_slot 3\n\t\t\t\t&trans          &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &trans  &fn_slot 4\n\t\t\t\t&trans  &trans  &trans                          &trans                  &trans  &trans  &trans  &trans  &trans  &trans\n\t\t\t\t&none &none &none &none &none &none\n\t\t\t>;\n\t\t};\n\t};\n};\n",
  "geometry": {
    "rowUnits": [
      [
        1.25,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1.25
      ],
      [
        1.25,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1.5,
        1.25
      ],
      [
        1.25,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1.25,
        1.25
      ],
      [
        1.5,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1.75,
        1.25
      ],
      [
        1.75,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1,
        1.75,
        1,
        1.25
      ],
      [
        1.25,
        1.25,
        1.25,
        5.5,
        1.25,
        1.25,
        1.25,
        1,
        1,
        1
      ],
      [
        1,
        1,
        1,
        1,
        1,
        1
      ]
    ],
    "rowGapBefore": [
      [
        0,
        0.5,
        0,
        0,
        0,
        0.5,
        0,
        0,
        0,
        0.5,
        0,
        0,
        0,
        0.75
      ],
      [
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0.75
      ],
      [
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        1
      ],
      [
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        1.25
      ],
      [
        0,
        0.5,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0.5
      ],
      [
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0.5,
        0,
        0
      ],
      [
        0,
        0,
        0,
        0,
        0,
        0
      ]
    ]
  },
  "layers": [
    {
      "id": "base",
      "label": "BASE",
      "rows": [
        [
          {
            "raw": "&kp ESC",
            "label": "ESC"
          },
          {
            "raw": "&kp F1",
            "label": "F1"
          },
          {
            "raw": "&kp F2",
            "label": "F2"
          },
          {
            "raw": "&kp F3",
            "label": "F3"
          },
          {
            "raw": "&kp F4",
            "label": "F4"
          },
          {
            "raw": "&kp F5",
            "label": "F5"
          },
          {
            "raw": "&kp F6",
            "label": "F6"
          },
          {
            "raw": "&kp F7",
            "label": "F7"
          },
          {
            "raw": "&kp F8",
            "label": "F8"
          },
          {
            "raw": "&kp F9",
            "label": "F9"
          },
          {
            "raw": "&kp F10",
            "label": "F10"
          },
          {
            "raw": "&kp F11",
            "label": "F11"
          },
          {
            "raw": "&kp F12",
            "label": "F12"
          },
          {
            "raw": "&fn_slot 0",
            "label": "FN_SLOT 0"
          }
        ],
        [
          {
            "raw": "&kp GRAVE",
            "label": "GRAVE"
          },
          {
            "raw": "&kp N1",
            "label": "N1"
          },
          {
            "raw": "&kp N2",
            "label": "N2"
          },
          {
            "raw": "&kp N3",
            "label": "N3"
          },
          {
            "raw": "&kp N4",
            "label": "N4"
          },
          {
            "raw": "&kp N5",
            "label": "N5"
          },
          {
            "raw": "&kp N6",
            "label": "N6"
          },
          {
            "raw": "&kp N7",
            "label": "N7"
          },
          {
            "raw": "&kp N8",
            "label": "N8"
          },
          {
            "raw": "&kp N9",
            "label": "N9"
          },
          {
            "raw": "&kp N0",
            "label": "N0"
          },
          {
            "raw": "&kp MINUS",
            "label": "MINUS"
          },
          {
            "raw": "&kp EQUAL",
            "label": "EQUAL"
          },
          {
            "raw": "&kp BSPC",
            "label": "BSPC"
          },
          {
            "raw": "&fn_slot 1",
            "label": "FN_SLOT 1"
          }
        ],
        [
          {
            "raw": "&kp TAB",
            "label": "TAB"
          },
          {
            "raw": "&kp Q",
            "label": "Q"
          },
          {
            "raw": "&kp W",
            "label": "W"
          },
          {
            "raw": "&kp E",
            "label": "E"
          },
          {
            "raw": "&kp R",
            "label": "R"
          },
          {
            "raw": "&kp T",
            "label": "T"
          },
          {
            "raw": "&kp Y",
            "label": "Y"
          },
          {
            "raw": "&kp U",
            "label": "U"
          },
          {
            "raw": "&kp I",
            "label": "I"
          },
          {
            "raw": "&kp O",
            "label": "O"
          },
          {
            "raw": "&kp P",
            "label": "P"
          },
          {
            "raw": "&kp LBKT",
            "label": "LBKT"
          },
          {
            "raw": "&kp RBKT",
            "label": "RBKT"
          },
          {
            "raw": "&kp BSLH",
            "label": "BSLH"
          },
          {
            "raw": "&fn_slot 2",
            "label": "FN_SLOT 2"
          }
        ],
        [
          {
            "raw": "&kp CLCK",
            "label": "CLCK"
          },
          {
            "raw": "&kp A",
            "label": "A"
          },
          {
            "raw": "&kp S",
            "label": "S"
          },
          {
            "raw": "&kp D",
            "label": "D"
          },
          {
            "raw": "&kp F",
            "label": "F"
          },
          {
            "raw": "&kp G",
            "label": "G"
          },
          {
            "raw": "&kp H",
            "label": "H"
          },
          {
            "raw": "&kp J",
            "label": "J"
          },
          {
            "raw": "&kp K",
            "label": "K"
          },
          {
            "raw": "&kp L",
            "label": "L"
          },
          {
            "raw": "&kp SEMI",
            "label": "SEMI"
          },
          {
            "raw": "&kp SQT",
            "label": "SQT"
          },
          {
            "raw": "&kp RET",
            "label": "RET"
          },
          {
            "raw": "&fn_slot 3",
            "label": "FN_SLOT 3"
          }
        ],
        [
          {
            "raw": "&kp LSHFT",
            "label": "LSHFT"
          },
          {
            "raw": "&kp Z",
            "label": "Z"
          },
          {
            "raw": "&kp X",
            "label": "X"
          },
          {
            "raw": "&kp C",
            "label": "C"
          },
          {
            "raw": "&kp V",
            "label": "V"
          },
          {
            "raw": "&kp B",
            "label": "B"
          },
          {
            "raw": "&kp N",
            "label": "N"
          },
          {
            "raw": "&kp M",
            "label": "M"
          },
          {
            "raw": "&kp COMMA",
            "label": "COMMA"
          },
          {
            "raw": "&kp DOT",
            "label": "DOT"
          },
          {
            "raw": "&kp FSLH",
            "label": "FSLH"
          },
          {
            "raw": "&kp RSHFT",
            "label": "RSHFT"
          },
          {
            "raw": "&kp UP",
            "label": "UP"
          },
          {
            "raw": "&fn_slot 4",
            "label": "FN_SLOT 4"
          }
        ],
        [
          {
            "raw": "&kp LCTRL",
            "label": "LCTRL"
          },
          {
            "raw": "&kp LGUI",
            "label": "LGUI"
          },
          {
            "raw": "&kp LALT",
            "label": "LALT"
          },
          {
            "raw": "&kp SPACE",
            "label": "SPACE"
          },
          {
            "raw": "&kp RALT",
            "label": "RALT"
          },
          {
            "raw": "&mo FN",
            "label": "MO FN"
          },
          {
            "raw": "&kp RCTRL",
            "label": "RCTRL"
          },
          {
            "raw": "&kp LEFT",
            "label": "LEFT"
          },
          {
            "raw": "&kp DOWN",
            "label": "DOWN"
          },
          {
            "raw": "&kp RIGHT",
            "label": "RIGHT"
          }
        ],
        [
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          }
        ]
      ]
    },
    {
      "id": "fn",
      "label": "FN",
      "rows": [
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&kp C_BRI_DEC",
            "label": "C_BRI_DEC"
          },
          {
            "raw": "&kp C_BRI_INC",
            "label": "C_BRI_INC"
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&kp C_PREV",
            "label": "C_PREV"
          },
          {
            "raw": "&kp C_PP",
            "label": "C_PP"
          },
          {
            "raw": "&kp C_NEXT",
            "label": "C_NEXT"
          },
          {
            "raw": "&kp C_MUTE",
            "label": "C_MUTE"
          },
          {
            "raw": "&kp C_VOL_DN",
            "label": "C_VOL_DN"
          },
          {
            "raw": "&kp C_VOL_UP",
            "label": "C_VOL_UP"
          },
          {
            "raw": "&fn_slot 0",
            "label": "FN_SLOT 0"
          }
        ],
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&fn_slot 1",
            "label": "FN_SLOT 1"
          }
        ],
        [
          {
            "raw": "&rgb_ug RGB_TOG",
            "label": "RGB TOG"
          },
          {
            "raw": "&rgb_ug RGB_EFF",
            "label": "RGB EFF"
          },
          {
            "raw": "&rgb_ug RGB_BRI",
            "label": "RGB BRI"
          },
          {
            "raw": "&rgb_ug RGB_HUI",
            "label": "RGB HUI"
          },
          {
            "raw": "&rgb_ug RGB_SAI",
            "label": "RGB SAI"
          },
          {
            "raw": "&rgb_ug RGB_SPI",
            "label": "RGB SPI"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&fn_slot 2",
            "label": "FN_SLOT 2"
          }
        ],
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&rgb_ug RGB_EFR",
            "label": "RGB EFR"
          },
          {
            "raw": "&rgb_ug RGB_BRD",
            "label": "RGB BRD"
          },
          {
            "raw": "&rgb_ug RGB_HUD",
            "label": "RGB HUD"
          },
          {
            "raw": "&rgb_ug RGB_SAD",
            "label": "RGB SAD"
          },
          {
            "raw": "&rgb_ug RGB_SPD",
            "label": "RGB SPD"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&fn_slot 3",
            "label": "FN_SLOT 3"
          }
        ],
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&fn_slot 4",
            "label": "FN_SLOT 4"
          }
        ],
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&tb_mode",
            "label": "TB MODE"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          }
        ],
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          }
        ]
      ]
    },
    {
      "id": "touch",
      "label": "Touch",
      "rows": [
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&fn_slot 0",
            "label": "FN_SLOT 0"
          }
        ],
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&fn_slot 1",
            "label": "FN_SLOT 1"
          }
        ],
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&fn_slot 2",
            "label": "FN_SLOT 2"
          }
        ],
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&fn_slot 3",
            "label": "FN_SLOT 3"
          }
        ],
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&fn_slot 4",
            "label": "FN_SLOT 4"
          }
        ],
        [
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          },
          {
            "raw": "&trans",
            "label": "TRANS"
          }
        ],
        [
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          },
          {
            "raw": "&none",
            "label": ""
          }
        ]
      ]
    }
  ]
} as const;

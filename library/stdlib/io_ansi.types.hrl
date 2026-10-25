%% OTP 29 io_ansi types: the exported types and the local types and records they use, as OTP defines
%% them; observed by tests/compiler/library/observe_types.escript (docs/library.md#types).
-export_type([vts/0]).
-type color_atom() ::
          black | blue | cyan | green | magenta | red | white | yellow |
          light_black | light_blue | light_cyan | light_green |
          light_magenta | light_red | light_white | light_yellow.
-type foreground_color() ::
          color_atom() |
          {color, 0..255} |
          {color, R :: 0..255, G :: 0..255, B :: 0..255} |
          default_color.
-type background_color() ::
          black_background | blue_background | cyan_background |
          green_background | magenta_background | red_background |
          white_background | yellow_background | default_background |
          light_black_background | light_blue_background |
          light_cyan_background | light_green_background |
          light_magenta_background | light_red_background |
          light_white_background | light_yellow_background |
          {background, 0..255} |
          {background, R :: 0..255, G :: 0..255, B :: 0..255}.
-type underline_color() ::
          black_underline | blue_underline | cyan_underline |
          green_underline | magenta_underline | red_underline |
          white_underline | yellow_underline | default_underline |
          light_black_underline | light_blue_underline |
          light_cyan_underline | light_green_underline |
          light_magenta_underline | light_red_underline |
          light_white_underline | light_yellow_underline |
          {underline_color, 0..255} |
          {underline_color, R :: 0..255, G :: 0..255, B :: 0..255}.
-type color() ::
          foreground_color() |
          background_color() |
          underline_color() |
          {modify_color,
           Index :: 0..255,
           R :: 0..255,
           G :: 0..255,
           B :: 0..255}.
-type underline_style() ::
          underline | underline_off | double_underline |
          curly_underline | dotted_underline | dashed_underline.
-type style() ::
          bold | bold_off | blink | blink_off | dim | dim_off |
          invisible | invisible_off | italic | italic_off | inverse |
          inverse_off | overline | overline_off | strikethrough |
          strikethrough_off |
          underline_style().
-type hyperlink_params() ::
          [{Key :: unicode:chardata(), Value :: unicode:chardata()}].
-type hyperlink() ::
          {hyperlink,
           URL :: uri_string:uri_string(),
           Text :: unicode:chardata()} |
          {hyperlink,
           URL :: uri_string:uri_string(),
           hyperlink_params(),
           Text :: unicode:chardata()} |
          {hyperlink_start, URL :: uri_string:uri_string()} |
          {hyperlink_start,
           URL :: uri_string:uri_string(),
           hyperlink_params()} |
          hyperlink_reset.
-type text_formatting() :: color() | style() | hyperlink().
-type text_modification() ::
          clear | erase_display | insert_character | delete_character |
          erase_character | insert_line | delete_line | erase_line.
-type text() ::
          text_formatting() |
          text_modification() |
          alternate_character_set_mode |
          alternate_character_set_mode_off.
-type cursor() ::
          {cursor,
           Line :: non_neg_integer(),
           Column :: non_neg_integer()} |
          {cursor_move,
           DeltaLine :: integer(),
           DeltaColumn :: integer()} |
          cursor_down | cursor_up | cursor_backward | cursor_forward |
          {cursor_down | cursor_backward | cursor_forward | cursor_up,
           N :: non_neg_integer()} |
          cursor_home | reverse_index | cursor_save | cursor_restore |
          cursor_show | cursor_hide | cursor_next_line |
          cursor_previous_line | cursor_horizontal_absolute |
          cursor_vertical_absolute | cursor_horizontal_vertical |
          cursor_report_position.
-type window() ::
          alternate_screen | alternate_screen_off | scroll_forward |
          scroll_backward | scroll_change_region.
-type tab() :: tab | tab_backward | tab_set | tab_clear | tab_clear_all.
-type input() ::
          keypad_transmit_mode | keypad_transmit_mode_off |
          kcursor_down | kcursor_up | kcursor_backward |
          kcursor_forward | kcursor_home | kcursor_end.
-type vts() ::
          text() |
          cursor() |
          window() |
          tab() |
          input() |
          reset | device_report_attributes.

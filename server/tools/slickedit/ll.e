#include "slick.sh"

static void cut_end_ws(_str line)
{
    end_line_text_toggle();
    if (pos('[\t ]+$', line, 1, 'r') > 0) {
        cut_end_line();
    }
}

_command void eol_semicolon() name_info(','VSARG2_MARK|VSARG2_REQUIRES_EDITORCTL)
{
    int start_col, end_col, buf_id;

    if (p_LangId != 'c') {
        c_semicolon();
        return;
    }

    if (_get_selinfo(start_col, end_col, buf_id) == 0) {
        c_semicolon();
        return;
    }
    if (_in_comment()) {
        c_semicolon();
        return;
    }
    get_line(auto line);
    
    if (pos('([\t ]+|^){for}[\t ]*\(', line, 1, 'r') > 0) {
        c_semicolon();
        return;
    }

    if (pos('\{[\t ]*$', line, 1, 'r') > 0) {
        c_semicolon();
        return;
    }
    if (pos(';[\t ]*$', line, 1, 'r') > 0) {
        cut_end_ws(line);
        return;
    }

    cut_end_ws(line);
    c_semicolon();
}

static int __cl_max;

static _str __del_cl(string)
{
    int i, n;

    n = pos('[\t \\]+$', string, 1, 'r');
    if (n > 0) {
        string = substr(string, 1, n - 1);
    }
    return string;
}

_command del_cl() name_info(','VSARG2_MARK|VSARG2_REQUIRES_EDITORCTL)
{
    if (_select_type()=="" ) {
       message(get_message(TEXT_NOT_SELECTED_RC));
       return(TEXT_NOT_SELECTED_RC);
    }
    return(filter_selection(__del_cl));
}

static _str __cl_get_max(string)
{
    if (string._length() > __cl_max) {
        __cl_max = string._length();
    }
    return string;
}

static _str __cl_set(string)
{
    int n;

    for (n = length(string); n < __cl_max; n++) {
        string = string :+ " ";
    }
    string = string :+ '\';
    return string;
}

_command add_cl() name_info(','VSARG2_MARK|VSARG2_REQUIRES_EDITORCTL)
{
    if (_select_type()=="" ) {
       message(get_message(TEXT_NOT_SELECTED_RC));
       return(TEXT_NOT_SELECTED_RC);
    }
    __cl_max = 0;
    (filter_selection(__cl_get_max));
    __cl_max++;
    return(filter_selection(__cl_set));
}

static _str __cl_get_max2(string)
{
    int n = pos('[\\][\t ]*$', string, 1, 'r');
    if (n > 0) {
        n = pos('[\t \\]+$', string, 1, 'r');
        if (n > __cl_max) {
            __cl_max = n;
        }
    }
    return string;
}

static _str __cl_set2(string)
{
    int n = pos('[\\][\t ]*$', string, 1, 'r');
    if (n > 0) {
        n = pos('[\t \\]+$', string, 1, 'r');
        if (n > 0) {
            string = substr(string, 1, n - 1);
        }
        for (; n < __cl_max; n++) {
            string = string :+ " ";
        }
        string = string :+ '\';
    }
    return string;
}

_command update_cl() name_info(','VSARG2_MARK|VSARG2_REQUIRES_EDITORCTL)
{
    if (_select_type()=="" ) {
       message(get_message(TEXT_NOT_SELECTED_RC));
       return(TEXT_NOT_SELECTED_RC);
    }
    __cl_max = 0;
    (filter_selection(__cl_get_max2));
    __cl_max++;
    return(filter_selection(__cl_set2));
}


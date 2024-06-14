#include <8051.h>
#include <string.h>

#define Fclk (12000000 / 12)
#define Fint 250
#define Treload (65536 - Fclk / Fint)
#define TH0_R (Treload >> 8)
#define TL0_R (Treload & 0xff)

#define MAX_STACK_SIZE 3
#define MAX_INFIX_EXPR_SIZE 15
#define MAX_HISTORY_SIZE 3
#define PRESS_AND_HOLD_THRESHOLD 1000

#define set(n, pos) led[pos] = n

/* 7-segment led table */
char seven_seg[] = {
    0x3f, 0x06, 0x5b, 0x4f,
    0x66, 0x6d, 0x7d, 0x07,
    0x7f, 0x6f, 0x77, 0x7c,
    0x58, 0x5e, 0x79, 0x71};

/* 7-segment led buffer */
char led[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/* base for calculator */
char bases[] = {10, 16, 2};
char base = 10;

/* infix expression */
char infix_expr[MAX_INFIX_EXPR_SIZE];
char infix_expr_len = 0;

/* floating point for division */
short floating_part;

/* circular queue for calculator history */
short history[MAX_HISTORY_SIZE];
char history_front = 0;
char history_rear = 0;

__sbit press_and_hold;
__sbit error;
__sbit has_floating_point;

void init(void);
char input(void);
char get_key(void);
char decode(char key);
void timer0(void) __interrupt(1) __using(2);
short calc(short a, short b, char op);
short infix_eval(char *infix);
void parse(char ch);

/* Initialize the 8051 timer and interrupt */
void init(void) {
    TMOD = 0x01; // timer 0, mode 1

    TR0 = 1; // timer 0 run enable
    TH0 = TH0_R;
    TL0 = TL0_R;

    EA = 1;  // enable global interrupt
    ET0 = 1; // enable timer 0 interrupt
}

/* Get the key from 4x4 keypad */
char input(void) {
    for (char i = 0; i < 4; ++i) {
        char mask = 1 << i;
        P0 &= 0xf0;
        P0 |= ~mask;
        for (char j = 0; j < 4; ++j) {
            if (~(P0 >> (4 + j)) & 1) {
                return j * 4 + i;
            }
        }
    }

    return 0xff;
}

/* Get and decode the key from 4x4 keypad */
char get_key(void) {
    char key;
    short cnt = 0;

    while (1) {
        /* Check whether 4x4 keypad is pressed */
        key = input();
        if (key != 0xff) {
            cnt = 0;

            /* Wait until the key is pressed for 10 times */
            while (cnt < 10) {
                if (input() == key) {
                    cnt++;
                } else {
                    cnt = 0;
                }
            }
            /* Wait until the key is released */
            while (input() != 0xff)
                cnt++;

            /* If the key is pressed for more than PRESS_AND_HOLD_THRESHOLD,
               then it is considered as press and hold */
            if (cnt > PRESS_AND_HOLD_THRESHOLD)
                press_and_hold = 1;
            else
                press_and_hold = 0;

            return decode(key);
        }
    }
}

/* Decode the key from 4x4 keypad */
char decode(char key) {
    /*
       PRESS           HOLD
    -----------     -----------
    | 7 8 9 / |     | M . . A |
    | 4 5 6 x | <-> | . . . B |
    | 1 2 3 - |     | . . . C |
    | 0 H = + |     | . F E D |
    -----------     -----------
    */
    switch (key) {
    case 0:
        return press_and_hold ? 'M' : '7';
    case 1:
        return press_and_hold ? ' ' : '8';
    case 2:
        return press_and_hold ? ' ' : '9';
    case 3:
        return press_and_hold ? 'A' : '/';
    case 4:
        return press_and_hold ? ' ' : '4';
    case 5:
        return press_and_hold ? ' ' : '5';
    case 6:
        return press_and_hold ? ' ' : '6';
    case 7:
        return press_and_hold ? 'B' : '*';
    case 8:
        return press_and_hold ? ' ' : '1';
    case 9:
        return press_and_hold ? ' ' : '2';
    case 10:
        return press_and_hold ? ' ' : '3';
    case 11:
        return press_and_hold ? 'C' : '-';
    case 12:
        return press_and_hold ? ' ' : '0';
    case 13:
        return press_and_hold ? 'F' : 'H';
    case 14:
        return press_and_hold ? 'E' : '=';
    case 15:
        return press_and_hold ? 'D' : '+';
    }
    return 0;
}

/* Timer 0 interrupt service routine, used to display 7-segment led */
void timer0(void) __interrupt(1) __using(1) {
    static char i = 0;
    TH0 = TH0_R;
    TL0 = TL0_R;

    /* Set position */
    P1 = i;

    /* Set value */
    if (led[i] != 0xff)
        P2 = led[i];
    else
        /* Display nothing */
        P2 = 0;

    i = (i + 1) % 8; // increase position
}

/* Calculate a op b */
short calc(short a, short b, char op) {
    switch (op) {
    case '+':
        return a + b;
    case '-':
        return a - b;
    case '*':
        return a * b;
    case '/':
        if (b == 0) { // divide zero
            error = 1;
            return 0;
        }

        float res = (float)a / b;
        short integer_part = (short)res;
        if (res - integer_part != 0) {
            has_floating_point = 1;
            floating_part = (res - integer_part) * 1000;
        }

        return integer_part;
    }
    return 0;
}

/* Evaluate the infix expression */
short infix_eval(char *infix) {
    short num_stack[MAX_STACK_SIZE]; // stack for numbers
    char num_top = 0;
    char op_stack[MAX_STACK_SIZE]; // stack for operators
    char op_top = 0;
    short tmp = 0; // temporary number
    __sbit is_negative = 0;

    for (char i = 0; i < infix_expr_len; i++) {
        char ch = infix[i];

        if (ch >= '0' && ch <= '9') {
            tmp = tmp * base + (ch - '0');

            /* If the last character is a number then push it to the stack */
            if (i == infix_expr_len - 1) {
                num_stack[num_top++] = is_negative ? -tmp : tmp;
                is_negative = 0;
            }
        } else if (ch >= 'A' && ch <= 'F') {
            tmp = tmp * base + (ch - 'A' + 10);

            /* If the last character is a number then push it to the stack */
            if (i == infix_expr_len - 1) {
                num_stack[num_top++] = is_negative ? -tmp : tmp;
                is_negative = 0;
            }
        } else {
            /* If the operator is minus, then the number is negative */
            if (ch == '-' && (i == 0 || infix[i - 1] == '+' || infix[i - 1] == '-' || infix[i - 1] == '*' || infix[i - 1] == '/')) {
                is_negative = 1;
                continue;
            }

            /* Push the temporary number to the stack */
            num_stack[num_top++] = is_negative ? -tmp : tmp;
            tmp = 0;
            is_negative = 0;

            switch (ch) {
            case '+':
            case '-':
                /* Pop operators which have higher or equal precedence than the current operator */
                while (op_top > 0 &&
                       (op_stack[op_top - 1] == '+' || op_stack[op_top - 1] == '-' ||
                        op_stack[op_top - 1] == '*' || op_stack[op_top - 1] == '/')) {
                    char op = op_stack[--op_top];
                    short b = num_stack[--num_top];
                    short a = num_stack[--num_top];

                    /* Calculate the result and push it to the num_stack */
                    num_stack[num_top++] = calc(a, b, op);
                    if (error) {
                        return 0;
                    }
                }

                /* Push the current operator to the op_stack */
                if (op_top < MAX_STACK_SIZE)
                    op_stack[op_top++] = ch;
                break;
            case '*':
            case '/':
                /* Pop operators which have higher or equal precedence than the current operator */
                while (op_top > 0 &&
                       (op_stack[op_top - 1] == '*' || op_stack[op_top - 1] == '/')) {
                    char op = op_stack[--op_top];
                    short b = num_stack[--num_top];
                    short a = num_stack[--num_top];

                    /* Calculate the result and push it to the num_stack */
                    num_stack[num_top++] = calc(a, b, op);
                    if (error) {
                        return 0;
                    }
                }

                /* Push the current operator to the op_stack */
                if (op_top < MAX_STACK_SIZE)
                    op_stack[op_top++] = ch;
                break;
            }
        }
    }

    /* Pop all operators and calculate the result */
    while (op_top > 0) {
        char op = op_stack[--op_top];
        short b = num_stack[--num_top];
        short a = num_stack[--num_top];
        num_stack[num_top++] = calc(a, b, op);
    }

    return num_stack[0];
}

/* Parse the input character */
void parse(char ch) {
    static short led_num = 0;    // input number to display in 7-segment led
    static char base_idx = 0;    // index for bases
    static char history_idx = 0; // index for history
    char led_idx;                // index for 7-segment led
    short tmp;

    switch (ch) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        if (base == 2 && ch > '1') {
            break;
        }

        /* Accumulate the number */
        led_num = led_num * base + (ch - '0');

        /* Display the number */
        memset(led, 0xff, sizeof(led));
        led_idx = 7;
        tmp = led_num;
        do {
            set(seven_seg[tmp % base], led_idx--);
            tmp /= base;
        } while (tmp > 0);

        /* Add the number to the infix expression */
        infix_expr[infix_expr_len++] = ch;
        break;
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
        if (base != 16) {
            break;
        }

        /* Accumulate the number */
        led_num = led_num * base + (ch - 'A' + 10);

        /* Display the number */
        memset(led, 0xff, sizeof(led));
        led_idx = 7;
        tmp = led_num;
        do {
            set(seven_seg[tmp % base], led_idx--);
            tmp /= base;
        } while (tmp > 0);

        /* Add the number to the infix expression */
        infix_expr[infix_expr_len++] = ch;
        break;
    case '+':
        led_num = 0;

        /* Display "Add" */
        memset(led, 0xff, sizeof(led));
        set(0x77, 5);
        set(0x5e, 6);
        set(0x5e, 7);

        /* Add the operator to the infix expression */
        infix_expr[infix_expr_len++] = ch;
        break;
    case '-':
        led_num = 0;

        /* Display "Sub" */
        memset(led, 0xff, sizeof(led));
        set(0x6d, 5);
        set(0x1c, 6);
        set(0x7c, 7);

        /* Add the operator to the infix expression */
        infix_expr[infix_expr_len++] = ch;
        break;
    case '*':
        led_num = 0;

        /* Display "Product" */
        memset(led, 0xff, sizeof(led));
        set(0x73, 1);
        set(0x50, 2);
        set(0x5c, 3);
        set(0x5E, 4);
        set(0x1c, 5);
        set(0x58, 6);
        set(0x78, 7);

        /* Add the operator to the infix expression */
        infix_expr[infix_expr_len++] = ch;
        break;
    case '/':
        led_num = 0;

        /* Display "Div" */
        memset(led, 0xff, sizeof(led));
        set(0x5e, 5);
        set(0x04, 6);
        set(0x1c, 7);

        /* Add the operator to the infix expression */
        infix_expr[infix_expr_len++] = ch;
        break;
    case '=':
        led_num = 0;

        /* Calculate the result */
        short result = infix_eval(infix_expr);

        /* If there is an error, display "Error" */
        if (error) {
            memset(led, 0xff, sizeof(led));
            set(0x79, 3);
            set(0x50, 4);
            set(0x50, 5);
            set(0x5c, 6);
            set(0x50, 7);

            /* Clear the infix expression */
            memset(infix_expr, 0, sizeof(infix_expr));
            infix_expr_len = 0;
            error = 0;
            break;
        }

        __sbit is_negative = result < 0;

        /* Add the result to the history */
        history[history_rear] = result;
        history_rear = (history_rear + 1) % MAX_HISTORY_SIZE;
        if (history_rear == history_front) {
            history_front = (history_front + 1) % MAX_HISTORY_SIZE;
        }

        led_idx = 7;
        memset(led, 0xff, sizeof(led));

        /* Check whether the result has a floating point */
        if (has_floating_point) {
            if (floating_part < 0) {
                floating_part = -floating_part;
            }
            do {
                set(seven_seg[floating_part % base], led_idx--);
                floating_part /= base;
            } while (floating_part > 0);
        }

        /* Display the result */
        if (is_negative) {
            result = -result;
        }
        do {
            set(seven_seg[result % base] | has_floating_point << 7, led_idx--);
            has_floating_point = 0;
            result /= base;
        } while (result > 0);

        /* If the result is negative, display the minus sign */
        if (is_negative) {
            set(0x40, led_idx--);
        }

        /* Clear the infix expression */
        memset(infix_expr, 0, sizeof(infix_expr));
        infix_expr_len = 0;

        /* Reset history_idx*/
        history_idx = history_rear - 1 < 0 ? MAX_HISTORY_SIZE - 1 : history_rear - 1;
        break;
    case 'H':
        tmp = history[history_idx];

        /* Update the history index */
        if (history_idx == history_front) {
            history_idx = history_rear - 1 < 0 ? MAX_HISTORY_SIZE - 1 : history_rear - 1;
        } else {
            history_idx = history_idx - 1 < 0 ? MAX_HISTORY_SIZE - 1 : history_idx - 1;
        }

        /* Display the history result */
        memset(led, 0xff, sizeof(led));
        led_idx = 7;
        do {
            set(seven_seg[tmp % base], led_idx--);
            tmp /= base;
        } while (tmp > 0);
        break;
    case 'M':
        /* Clear the infix expression */
        memset(infix_expr, 0, sizeof(infix_expr));
        infix_expr_len = 0;

        /* Change the base */
        base_idx = (base_idx + 1) % 3;
        base = bases[base_idx];

        /* Display "BASS" */
        memset(led, 0xff, sizeof(led));
        set(0x7F, 0);
        set(0x77, 1);
        set(0x6d, 2);
        set(0x6d, 3);

        /* Display the base */
        tmp = base;
        led_idx = 7;
        do {
            set(seven_seg[tmp % 10], led_idx--);
            tmp /= 10;
        } while (tmp > 0);
        break;
    default:
        break;
    }
}

void main(void) {
    char ch;

    init();

    while (1) {
        ch = get_key();

        EA = 0;
        parse(ch);
        EA = 1;
    }
}

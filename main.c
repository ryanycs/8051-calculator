#include <8051.h>
#include <string.h>

#define Fclk (12000000 / 12)
#define Fint 250
#define Treload (65536 - Fclk / Fint)
#define TH0_R (Treload >> 8)
#define TL0_R (Treload & 0xff)

#define STACK_SIZE 8
#define MAX_HISTORY 10

#define set(n, pos) led[pos] = n

/* 7-segment led table */
char seven_seg[] = {
    0x3f, 0x06, 0x5b, 0x4f,
    0x66, 0x6d, 0x7d, 0x07,
    0x7f, 0x6f, 0x77, 0x7c,
    0x58, 0x5e, 0x79, 0x71,
    // 0x1c, 0x37, 0x07, 0x04, /* letter: (u,v), m1, m2, i */
    // 0x40,                   /* minus(-) */
};

/* 7-segment led buffer */
char led[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/* input number to display in 7-segment led */
short led_num = 0;

/* infix expression */
char infix_expr[10];
char infix_expr_len = 0;

/* circular queue for calculator history */
char history[MAX_HISTORY];
char history_front = 0;
char history_rear = 0;

void init(void);
char input(void);
char get_key(void);
char decode(char key);
void timer0(void) __interrupt(1) __using(1);
short calc(short a, short b, char op);
short infix_eval(char *infix);
void execute(char ch);

void init(void) {
    TMOD = 0x01; // timer 0, mode 1

    TR0 = 1; // timer 0 run enable
    TH0 = TH0_R;
    TL0 = TL0_R;

    EA = 1;  // enable global interrupt
    ET0 = 1; // enable timer 0 interrupt
}

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

/* Debounce */
char get_key(void) {
    while (1) {
        char key = input();
        if (key != 0xff) {
            char cnt = 0;
            while (cnt < 10) {
                if (input() == key) {
                    cnt++;
                } else {
                    cnt = 0;
                }
            }
            while (input() != 0xff)
                ;
            return key;
        }
    }
}

char decode(char key) {
    /*
    key map
    -----------    -----------
    | 0 1 2 3 |    | 7 8 9 / |
    | 4 5 6 7 | -> | 4 5 6 x |
    | 8 9 A B |    | 1 2 3 - |
    | C D E F |    | 0   = + |
    -----------    -----------
    */
    // char calculator_keys[] = {'7', '8', '9', '/', '4', '5', '6', 'x', '1', '2', '3', '-', '0', ' ', '=', '+'};
    switch (key) {
    case 12:
        return '0';
    case 8:
        return '1';
    case 9:
        return '2';
    case 10:
        return '3';
    case 4:
        return '4';
    case 5:
        return '5';
    case 6:
        return '6';
    case 0:
        return '7';
    case 1:
        return '8';
    case 2:
        return '9';
    case 15:
        return '+';
    case 11:
        return '-';
    case 7:
        return '*';
    case 3:
        return '/';
    case 14:
        return '=';
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

/* Calculate the result of the operation */
short calc(short a, short b, char op) {
    switch (op) {
    case '+':
        return a + b;
    case '-':
        return a - b;
    case '*':
        return a * b;
    case '/':
        return a / b;
    }
    return 0;
}

/* Evaluate the infix expression */
short infix_eval(char *infix) {
    short num_stack[STACK_SIZE]; // stack for numbers
    char num_top = 0;
    char op_stack[STACK_SIZE]; // stack for operators
    char op_top = 0;
    // char idx = 0;  // index of infix
    short tmp = 0; // temporary number

    for (char i = 0; i < infix_expr_len; i++) {
        char ch = infix[i];

        if (ch >= '0' && ch <= '9') {
            tmp = tmp * 10 + (ch - '0');

            /* If the last character is a number then push it to the stack */
            if (i == infix_expr_len - 1) {
                num_stack[num_top++] = tmp;
            }
        } else {
            /* Push the temporary number to the stack */
            num_stack[num_top++] = tmp;
            tmp = 0;

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
                }

                /* Push the current operator to the op_stack */
                if (op_top < STACK_SIZE)
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
                }

                /* Push the current operator to the op_stack */
                if (op_top < STACK_SIZE)
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

/* Execute the operation */
void execute(char ch) {
    char led_idx;

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
        /* Accumulate the number */
        led_num = led_num * 10 + (ch - '0');

        /* Display the number */
        memset(led, 0xff, sizeof(led));
        led_idx = 7;
        short tmp = led_num;
        do {
            set(seven_seg[tmp % 10], led_idx--);
            tmp /= 10;
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

        /* Display "Mul" */
        memset(led, 0xff, sizeof(led));
        set(0x37, 4);
        set(0x07, 5);
        set(0x1c, 6);
        set(0x06, 7);

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
        char is_negative = result < 0;

        /* Display the result */
        if (is_negative) {
            result = -result;
        }
        memset(led, 0xff, sizeof(led));
        led_idx = 7;
        do {
            set(seven_seg[result % 10], led_idx--);
            result /= 10;
        } while (result > 0);

        /* If the result is negative, display the minus sign */
        if (is_negative) {
            set(0x40, led_idx--);
        }

        /* Clear the infix expression */
        memset(infix_expr, 0, sizeof(infix_expr));
        infix_expr_len = 0;
        break;
    default:
        break;
    }
}

void main(void) {
    init();

    while (1) {
        char ch = decode(get_key());
        execute(ch);
    }
}

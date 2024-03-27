#include <print.h>

#define IsDigit(x) (((x) >= '0') && ((x) <= '9'))

/* forward declaration */
static void print_char(fmt_callback_t, void *, char, int, int);
static void print_str(fmt_callback_t, void *, const char *, int, int);
static void print_num(fmt_callback_t, void *, unsigned long, int, int, int, int, char, int);

void vprintfmt(fmt_callback_t out, void *data, const char *fmt, va_list ap) {
	char c;
	const char *s;
	long num;
	
	// 标记输出宽度
	int width;
	// 是否为long型
	int long_flag; // output is long (rather than int)
	// 是否为负数
	int neg_flag;  // output is negative
	// 是否左对齐
	int ladjust;   // output is left-aligned
	// 填充多余位置所用字符
	char padc;     // padding char

	for (;*fmt;) {
		if (*fmt!='%') {
			print_char(out,data,(*fmt),1,0);
			fmt++;
			continue;
		}

		fmt++;

		width=0;
		long_flag=0;
		neg_flag=0;
		ladjust=0;
		padc=' ';

		/* check format flag */
		/* Exercise 1.4: Your code here. (5/8) */
		if(*fmt=='-') {
			ladjust=1;
			fmt++;
		}
		if(*fmt=='0') {
			padc='0';
			fmt++;
		}

		while (IsDigit(*fmt))
		{
			width=width*10+(*fmt-'0');
			fmt++;
		}

		if(*fmt=='l') {
			long_flag=1;
			fmt++;
		}

		switch (*fmt) {
		case 'b':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}
			print_num(out, data, num, 2, 0, width, ladjust, padc, 0);
			break;

		case 'd':
		case 'D':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}

			/*
			 * Refer to other parts (case 'b', case 'o', etc.) and func 'print_num' to
			 * complete this part. Think the differences between case 'd' and the
			 * others. (hint: 'neg_flag').
			 */
			/* Exercise 1.4: Your code here. (8/8) */
      if(num<0) {
        neg_flag=1;
        num=-1*num;
      }
      print_num(out, data, num, 10, neg_flag, width, ladjust, padc, 0);

			break;

		case 'o':
		case 'O':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}
			print_num(out, data, num, 8, 0, width, ladjust, padc, 0);
			break;

		case 'u':
		case 'U':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}
			print_num(out, data, num, 10, 0, width, ladjust, padc, 0);
			break;

		case 'x':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}
			print_num(out, data, num, 16, 0, width, ladjust, padc, 0);
			break;

		case 'X':
			if (long_flag) {
				num = va_arg(ap, long int);
			} else {
				num = va_arg(ap, int);
			}
			print_num(out, data, num, 16, 0, width, ladjust, padc, 1);
			break;

		case 'c':
			c = (char)va_arg(ap, int);
			print_char(out, data, c, width, ladjust);
			break;

		case 's':
			s = (char *)va_arg(ap, char *);
			print_str(out, data, s, width, ladjust);
			break;

		case '\0':
			fmt--;
			break;

		default:
			/* output this char as it is */
			out(data, fmt, 1);
		}
		fmt++;
	}
}

/* --------------- local help functions --------------------- */
void print_char(fmt_callback_t out, void *data, char c, int length, int ladjust) {
	int i;

	if (length < 1) {
		length = 1;
	}
	const char space = ' ';
	if (ladjust) {
		out(data, &c, 1);
		for (i = 1; i < length; i++) {
			out(data, &space, 1);
		}
	} else {
		for (i = 0; i < length - 1; i++) {
			out(data, &space, 1);
		}
		out(data, &c, 1);
	}
}

void print_str(fmt_callback_t out, void *data, const char *s, int length, int ladjust) {
	int i;
	int len = 0;
	const char *s1 = s;
	while (*s1++) {
		len++;
	}
	if (length < len) {
		length = len;
	}

	if (ladjust) {
		out(data, s, len);
		for (i = len; i < length; i++) {
			out(data, " ", 1);
		}
	} else {
		for (i = 0; i < length - len; i++) {
			out(data, " ", 1);
		}
		out(data, s, len);
	}
}

void print_num(fmt_callback_t out, void *data, unsigned long u, int base, int neg_flag, int length,
	       int ladjust, char padc, int upcase) {
	/* algorithm :
	 *  1. prints the number from left to right in reverse form.
	 *  2. fill the remaining spaces with padc if length is longer than
	 *     the actual length
	 *     TRICKY : if left adjusted, no "0" padding.
	 *		    if negtive, insert  "0" padding between "0" and number.
	 *  3. if (!ladjust) we reverse the whole string including paddings
	 *  4. otherwise we only reverse the actual string representing the num.
	 */

	int actualLength = 0;
	char buf[length + 70];
	char *p = buf;
	int i;

	do {
		int tmp = u % base;
		if (tmp <= 9) {
			*p++ = '0' + tmp;
		} else if (upcase) {
			*p++ = 'A' + tmp - 10;
		} else {
			*p++ = 'a' + tmp - 10;
		}
		u /= base;
	} while (u != 0);

	if (neg_flag) {
		*p++ = '-';
	}

	/* figure out actual length and adjust the maximum length */
	actualLength = p - buf;
	if (length < actualLength) {
		length = actualLength;
	}

	/* add padding */
	if (ladjust) {
		padc = ' ';
	}
	if (neg_flag && !ladjust && (padc == '0')) {
		for (i = actualLength - 1; i < length - 1; i++) {
			buf[i] = padc;
		}
		buf[length - 1] = '-';
	} else {
		for (i = actualLength; i < length; i++) {
			buf[i] = padc;
		}
	}

	/* prepare to reverse the string */
	int begin = 0;
	int end;
	if (ladjust) {
		end = actualLength - 1;
	} else {
		end = length - 1;
	}

	/* adjust the string pointer */
	while (end > begin) {
		char tmp = buf[begin];
		buf[begin] = buf[end];
		buf[end] = tmp;
		begin++;
		end--;
	}

	out(data, buf, length);
}

int vscanfmt(scan_callback_t in, void *data, const char *fmt, va_list ap) {
	int *ip;
	char *cp;
	char ch;
	int base, num, neg, ret=0;
	//ret is the num of var
	while(*fmt) {
		if(*fmt=='%') {
			ret++;
			fmt++;	// jump %
			
			do {
				in(data, &ch, 1);
			} while(ch==' ' || ch=='\t' || ch=='\n');	//jump blank
			// now ch is the first vavild
			switch(*fmt) {
				case 'd':
					// get point of address
					ip = (int *)va_arg(ap, int *);
					num=0;
					base = 10;
					
					if(ch=='-') {
						neg=1;
						in(data, &ch, 1);
					}
					else {
						neg=0;
					}

					while(IsDigit(ch)) {
						num = num*base + ch-'0';
						in(data, &ch, 1);
					}
					num = neg ? -1*num : num;
					*ip=num;

					break;
				case 'x':
					ip = (int *)va_arg(ap, int *);
					num=0;
					base = 16;
					
					if(ch=='-') {
						neg=1;
						in(data, &ch, 1);
					}
					else {
						neg=0;
					}

					while(IsDigit(ch) || (ch>='a' && ch<='f')) {
						if(IsDigit(ch)) {
							num = num*base + ch-'0';
						}
						else {
							num = num*base + ch-'a'+10;
						}
						in(data, &ch, 1);
					}
					num = neg ? -1*num : num;
					*ip=num;

					break;
				case 'c':
					cp = (char *)va_arg(ap, char *);
					*cp=ch;

					break;
				case 's':
					cp = (char *)va_arg(ap, char *);
					
					while(ch) {
						*cp=ch;
						cp++;
						in(data, &ch, 1);
					}
					*cp=0;

					break;
			}
			fmt++;
		}
	}
	
	return ret;
}

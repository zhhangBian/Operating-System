#include <stdio.h>
#include <string.h>

int isFitOrder(char str[]) {
	int len=strlen(str);
	for(int i=0;i<len/2;i++) {
		if(str[i] != str[len-1-i])
			return 0;
	}

	return 1;
}


int main() {
	int n;
	scanf("%d", &n);
	char str[10];
	int pos=0;

	while(n) {
		str[pos++] = n%10;
		n=n/10;
	}

	if (isFitOrder(str)) {
		printf("Y\n");
	} else {
		printf("N\n");
	}
	return 0;
}

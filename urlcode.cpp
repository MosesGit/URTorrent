#include "urlcode.h"
#include <openssl/sha.h>

using namespace std;
unsigned char c2h(unsigned char x) 
{ 
	if(x>9)
		return x+55;
	else
		return x+48;
}

unsigned char h2c(unsigned char x) 
{ 
    unsigned char y;
    if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
    else if (x >= '0' && x <= '9') y = x - '0';
    return y;
}

string encode(const string& str)
{
	string strTemp = "";
	for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
	{
		if (isalnum((unsigned char)str[i]) || (str[i] == '-') || (str[i] == '_') || (str[i] == '.') || (str[i] == '~'))
			strTemp += str[i];
		else
		{
			strTemp += '%';
			strTemp += c2h((unsigned char)str[i] >> 4);
			strTemp += c2h((unsigned char)str[i] % 16);
		}
	}
	return strTemp;
}

string decode(const string& str)
{
	string strTemp = "";
	for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
	{
		if (str[i] == '%')
		{
            
		unsigned char high = h2c((unsigned char)str[++i]);
		unsigned char low = h2c((unsigned char)str[++i]);
		strTemp += high*16 + low;
		}
		else strTemp += str[i];
	}
	return strTemp;
}

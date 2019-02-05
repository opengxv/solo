#include "charset.h"
#include <limits>
#ifdef GX_USE_ICONV
#include "iconv.h"
#endif


GX_NS_BEGIN

ptr<Data> Charset::convert(const Data &source, const char *from, const char *to) noexcept {
#ifdef GX_USE_ICONV
	iconv_t cd = iconv_open(to, from);
	if (cd == (iconv_t)-1) {
		return nullptr;
	}

	char *inbuf = source.data();
	char *outbuf = nullptr;
	size_t insize = source.size();
	size_t outsize = std::numeric_limits<size_t>::max();
	int n = iconv(cd, &inbuf, &insize, &outbuf, &outsize);
	iconv_close(cd);

	if (n < 0 || insize) {
		return nullptr;
	}


	insize = source.size();
	outsize = std::numeric_limits<size_t>::max() - outsize;
	object<Data> result(outsize);
	inbuf = source.data();
	outbuf = result->data();

	cd = iconv_open(to, from);
	if (cd == (iconv_t)-1) {
		return nullptr;
	}
	iconv(cd, &inbuf, &insize, &outbuf, &outsize);
	iconv_close(cd);

	return result;
#else
	object<Data> result(source.size());
	memcpy(result->data(), source.data(), source.size());
	return result;
#endif
}

GX_NS_END


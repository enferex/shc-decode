#include <assert.h>
#include <b64/cdecode.h>
#include <ctype.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

static bool scan_token(FILE *fp, const char *token) {
  assert(fp && token && "Invalid input.");

  size_t len = strlen(token);
  char *buf = calloc(len + 1, 1);
  if (!buf) {
    fprintf(stderr, "Failed to allocate token buffer.");
    exit(EXIT_FAILURE);
  }

  if (fread(buf, 1, len, fp) != len) {
    fprintf(stderr, "Failed to read requested bytes.");
    exit(EXIT_FAILURE);
  }

  bool ok = strncmp(buf, token, len) == 0;
  free(buf);
  return ok;
}

typedef struct _pair_t {
  int a, b;
} pair_t;

// This extracts 2 bytes from the input file.
// These are base-10 numbers in ascii (0-9).
static bool read_pair(FILE *fp, pair_t *pr) {
  assert(pr && "Null pr discovered.");
  pr->a = fgetc(fp);
  pr->b = fgetc(fp);
  return !ferror(fp) && pr->a != EOF && pr->b != EOF;
}

// Decode the qr values (all digits) into UTF-8.  This stops decoding at the
// section delimiter '.'.
static size_t decode_qr_section_into_ascii(FILE *fp, uint8_t *buf,
                                           size_t buf_size) {
  assert(fp && buf && "Null file pointer and/or output buffer.");

  // Temporary decode buffer: QR digits into base64 encoded ascii.
  uint8_t *b64ascii = calloc(buf_size, 1);
  if (!b64ascii) {
    fprintf(stderr, "Failed to allocate b64ascii.\n");
    exit(EXIT_FAILURE);
  }

  pair_t pr;
  size_t buf_idx = 0;
  while (buf_idx < buf_size && read_pair(fp, &pr)) {
    // See https://spec.smarthealth.cards/#encoding-chunks-as-qr-codes
    int a = pr.a;  // This is an ascii digit (0-9)
    int b = pr.b;  // This is an ascii digit (0-9)
    assert(isdigit(a) && isdigit(b) && "Invalid ascii digit.");
    int a_val = 9 - ('9' - a);
    int b_val = 9 - ('9' - b);
    int c = 45 + ((10 * a_val) + b_val);
    assert(a_val < 10 && b_val < 10 && "Invalid encoding.");
    assert(isascii(c) && "Invalid ascii character.");
    if (c == '.')
      break;
    else if (c == '-')
      c = '+';
    else if (c == '_')
      c = '/';
    b64ascii[buf_idx++] = (char)c;
  }

  // Decode the b64 encoded ascii buffer.
  base64_decodestate state;
  base64_init_decodestate(&state);
  if (base64_decode_block((char *)b64ascii, buf_idx, (char *)buf, &state) < 0) {
    fprintf(stderr, "Failed to decode base64 buffer.\n");
    exit(EXIT_FAILURE);
  }

  free(b64ascii);
  return buf_idx;
}

static size_t get_filesize(FILE *fp) {
  assert(fp && "Null file pointer.");
  struct stat st;
  if (fstat(fileno(fp), &st)) {
    fprintf(stderr, "Error calling fstat on input file.\n");
    exit(EXIT_FAILURE);
  }
  return (size_t)st.st_size;
}

static void read_jws_header(FILE *fp) {
  assert(fp && "Null file pointer.");
  const size_t buf_size = get_filesize(fp);
  char *buf = calloc(buf_size, 1);
  if (!buf) {
    fprintf(stderr, "Failed to allocate output buffer.\n");
    exit(EXIT_FAILURE);
  }

  decode_qr_section_into_ascii(fp, (uint8_t *)buf, buf_size);
  buf[buf_size - 1] = '\0';
  printf("%s", buf);
  free(buf);
}

static void read_jws_payload(FILE *fp) {
  assert(fp && "NULL file pointer.");

  // This should be plenty wide for the payload... I think?
  const size_t file_size = get_filesize(fp);
  char *buf = calloc(file_size, 1);
  if (!buf) {
    fprintf(stderr, "Failed to allocate deflate buffer.");
    exit(EXIT_FAILURE);
  }

  // Decode QR digits in the payload.
  const size_t section_size =
      decode_qr_section_into_ascii(fp, (uint8_t *)buf, file_size);

  // Squish each 4 byte ascii into a 4byte uint32.
  const size_t wide_size = section_size;
  uint32_t *wide = calloc(wide_size, 1);
  for (size_t i = 0, j = 0; i < section_size; i += 4, ++j) {
    uint8_t a = buf[i + 0];
    uint8_t b = (i + 1) < section_size ? buf[i + 1] : 0;
    uint8_t c = (i + 2) < section_size ? buf[i + 2] : 0;
    uint8_t d = (i + 3) < section_size ? buf[i + 3] : 0;
    wide[j] = (uint32_t)(d << 24 | c << 16 | b << 8 | a);
  }

  // Decoded buffer (hopefully this is wide enough?).
  const size_t inf_size = file_size * sizeof(uint32_t);
  char *inf_buf = calloc(inf_size, 1);
  if (!inf_buf) {
    fprintf(stderr, "Failed to allocate DEFLATE buffer.\n");
    exit(EXIT_FAILURE);
  }

  // Run DEFLATE on the buf. (See https://zlib.net/zpipe.c)
  z_stream zs = {0};
  zs.avail_in = wide_size;
  zs.next_in = (Byte *)wide;
  zs.avail_out = inf_size;
  zs.next_out = (Bytef *)inf_buf;
  if (inflateInit2(&zs, -15) != Z_OK) {  // Raw DEFLATE decoding.
    fprintf(stderr, "Failed to initialize DEFLATE buffer: %s\n", zs.msg);
    exit(EXIT_FAILURE);
  }
  if (inflate(&zs, Z_FINISH) != Z_STREAM_END) {
    fprintf(stderr, "Failed to decompress buffer: %s\n", zs.msg);
    exit(EXIT_FAILURE);
  }
  inflateEnd(&zs);

  for (int i = 0; i < inf_size; ++i)
    if (inf_buf[i] == 0)
      break;
    else
      putc(inf_buf[i], stdout);

  free(buf);
  free(wide);
  free(inf_buf);
}

static void read_qr(FILE *fp) {
  assert(fp && "NULL file pointer.");
  if (!scan_token(fp, "shc:/")) {
    fprintf(stderr, "Failed to locate initial shc:/ token.\n");
    exit(EXIT_FAILURE);
  }

  read_jws_header(fp);
  putc('\n', stdout);
  read_jws_payload(fp);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <qr code .txt>\n", argv[0]);
    return 0;
  }

  const char *fname = argv[1];
  FILE *fp = fopen(fname, "r");
  read_qr(fp);
  fclose(fp);

  return 0;
}

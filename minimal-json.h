#ifndef MJSON_H_
#define MJSON_H_

#define MJSON_TYPE_NULL 1
#define MJSON_TYPE_TRUE 2
#define MJSON_TYPE_FALSE 3
#define MJSON_TYPE_STRING 4
#define MJSON_TYPE_NUMBER 5
#define MJSON_TYPE_OBJECT 6
#define MJSON_TYPE_ARRAY 7

#define MJSON_SUBTYPE_OBJECT_SEPARATOR 8
#define MJSON_SUBTYPE_OBJECT_END 9
#define MJSON_SUBTYPE_ARRAY_SEPARATOR 10
#define MJSON_SUBTYPE_ARRAY_END 11

#define MJSON_OK 0
#define MJSON_ERROR_READING -1
#define MJSON_ERROR_UNKNOWN_TYPE -2
#define MJSON_ERROR_CHECK_FAILURE -3
#define MJSON_ERROR_TEST_NOT_TRUE -4

/* This should at least be 5 to hold `false` */
#define MJSON_BUFFER_MAX_LENGTH 8

typedef int16_t mjson_status_t;

struct mjson_ctx;

typedef size_t (*mjson_reader)(struct mjson_ctx *ctx, char *data, size_t limit);

struct mjson_ctx {
  void *userdata;
  mjson_reader reader;
  char buffer[MJSON_BUFFER_MAX_LENGTH];
  size_t start;
  size_t length;
};

#define MJSON_BUFFER_START_(ctx) ((ctx)->buffer + (ctx)->start)

/* TODO: we can accept an optional length, so we won't read more data than
 * needed when dealing with numbers directly
 */
void mjson_init(struct mjson_ctx *ctx, void *userdata, mjson_reader reader)
{
  ctx->userdata = userdata;
  ctx->reader = reader;
  ctx->start = 0;
  ctx->length = 0;
}

mjson_status_t mjson_skip_value(struct mjson_ctx *ctx);

void mjson_shift_buffer_(struct mjson_ctx *ctx)
{
  size_t i;
  for (i = 0; i < ctx->length; i++) {
    ctx->buffer[i] = ctx->buffer[i + ctx->start];
  }
  ctx->start = 0;
}

mjson_status_t
mjson_ensure_byte_(struct mjson_ctx *ctx, size_t byte)
{
  if (ctx->length >= byte) { return MJSON_OK; }
  byte -= ctx->length;
  if (MJSON_BUFFER_MAX_LENGTH - ctx->length - ctx->start < byte) {
    mjson_shift_buffer_(ctx);
  }
  if (ctx->reader(ctx, &ctx->buffer[ctx->start + ctx->length], byte) < byte) {
    return MJSON_ERROR_READING;
  }
  ctx->length += byte;
  return MJSON_OK;
}

void
mjson_consume_byte_(struct mjson_ctx *ctx, size_t byte)
{
  ctx->start += byte;
  ctx->length -= byte;
}

mjson_status_t
mjson_read_byte_(struct mjson_ctx *ctx, char ch)
{
  mjson_status_t status;
  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }

  if (*MJSON_BUFFER_START_(ctx) == ch) {
    mjson_consume_byte_(ctx, 1);
    return MJSON_OK;
  }
  return MJSON_ERROR_TEST_NOT_TRUE;
}

mjson_status_t
mjson_equal_string_(const char* a, const char* b, size_t n) {
  size_t i;
  for (i = 0; i < n && a[i] && a[i] == b[i]; i++) ;
  return i == n ? MJSON_OK : MJSON_ERROR_TEST_NOT_TRUE;
}

mjson_status_t
mjson_read_type(struct mjson_ctx *ctx)
{
  mjson_status_t status;
  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }

  switch (*MJSON_BUFFER_START_(ctx)) {
    case '"':
      mjson_consume_byte_(ctx, 1);
      return MJSON_TYPE_STRING;
    case '{':
      mjson_consume_byte_(ctx, 1);
      return MJSON_TYPE_OBJECT;
    case '[':
      mjson_consume_byte_(ctx, 1);
      return MJSON_TYPE_ARRAY;
    case 't':
      status = mjson_ensure_byte_(ctx, 4);
      if (status != MJSON_OK) { return status; }
      if (mjson_equal_string_(MJSON_BUFFER_START_(ctx), "true", 4) == MJSON_OK) {
        mjson_consume_byte_(ctx, 4);
        return MJSON_TYPE_TRUE;
      } else {
        return MJSON_ERROR_UNKNOWN_TYPE;
      }
      break;
    case 'f':
      status = mjson_ensure_byte_(ctx, 5);
      if (status != MJSON_OK) { return status; }
      if (mjson_equal_string_(MJSON_BUFFER_START_(ctx), "false", 5) == MJSON_OK) {
        mjson_consume_byte_(ctx, 5);
        return MJSON_TYPE_FALSE;
      } else {
        return MJSON_ERROR_UNKNOWN_TYPE;
      }
      break;
    case 'n':
      status = mjson_ensure_byte_(ctx, 4);
      if (status != MJSON_OK) { return status; }
      if (mjson_equal_string_(MJSON_BUFFER_START_(ctx), "null", 4) == MJSON_OK) {
        mjson_consume_byte_(ctx, 4);
        return MJSON_TYPE_NULL;
      } else {
        return MJSON_ERROR_UNKNOWN_TYPE;
      }
      break;
    case '-':
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
      return MJSON_TYPE_NUMBER;
  }
  return MJSON_ERROR_UNKNOWN_TYPE;
}

mjson_status_t mjson_readcheck_null(struct mjson_ctx *ctx)
{
  return mjson_read_type(ctx) == MJSON_TYPE_NULL ?
      MJSON_OK : MJSON_ERROR_CHECK_FAILURE;
}

mjson_status_t mjson_readcheck_boolean(struct mjson_ctx *ctx, int8_t *b)
{
  mjson_status_t t = mjson_read_type(ctx);
  switch (t) {
    case MJSON_TYPE_TRUE:
      *b = 1;
      return MJSON_OK;
    case MJSON_TYPE_FALSE:
      *b = 0;
      return MJSON_OK;
  }
  return MJSON_ERROR_CHECK_FAILURE;
}

mjson_status_t mjson_readcheck_string_start(struct mjson_ctx *ctx)
{
  return mjson_read_type(ctx) == MJSON_TYPE_STRING ?
      MJSON_OK : MJSON_ERROR_CHECK_FAILURE;
}

mjson_status_t mjson_read_partial_string(struct mjson_ctx *ctx,
                                         char *data, size_t length,
                                         size_t *out_length)
{
  mjson_status_t status;
  size_t i = 0;

  while (i < length) {
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }

    if (*MJSON_BUFFER_START_(ctx) == '"') {
      break;
    } else if (*MJSON_BUFFER_START_(ctx) == '\\') {
      status = mjson_ensure_byte_(ctx, 2);
      if (status != MJSON_OK) { return status; }
      switch (MJSON_BUFFER_START_(ctx)[1]) {
        case '"':
          data[i++] = '"';
          break;
        case '\\':
          data[i++] = '\\';
          break;
        case '/':
          data[i++] = '/';
          break;
        case 'b':
          data[i++] = '\b';
          break;
        case 'f':
          data[i++] = '\f';
          break;
        case 'n':
          data[i++] = '\n';
          break;
        case 'r':
          data[i++] = '\r';
          break;
        case 't':
          data[i++] = '\t';
          break;
        default:
          return MJSON_ERROR_UNKNOWN_TYPE;
      }
      mjson_consume_byte_(ctx, 2);
    } else {
      data[i++] = *MJSON_BUFFER_START_(ctx);
      mjson_consume_byte_(ctx, 1);
    }
  }

  *out_length = i;
  return MJSON_OK;
}

mjson_status_t mjson_read_string_end(struct mjson_ctx *ctx)
{
  return mjson_read_byte_(ctx, '"');
}

/* After this you don't have to do mjson_read_string_end */
mjson_status_t mjson_read_full_string(struct mjson_ctx *ctx,
                                      char *data, size_t length,
                                      size_t *out_length)
{
  mjson_status_t status;
  size_t full_length;
  status = mjson_read_partial_string(ctx, data, length, &full_length);
  if (status != MJSON_OK) { return status; }

  while (1) {
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }

    if (*MJSON_BUFFER_START_(ctx) == '"') {
      mjson_consume_byte_(ctx, 1);
      *out_length = full_length;
      return MJSON_OK;
    } else if (*MJSON_BUFFER_START_(ctx) == '\\') {
      status = mjson_ensure_byte_(ctx, 2);
      if (status != MJSON_OK) { return status; }
      mjson_consume_byte_(ctx, 2);
      full_length++;
    } else {
      mjson_consume_byte_(ctx, 1);
      full_length++;
    }
  }
  /* Not reachable */
  return MJSON_ERROR_CHECK_FAILURE;
}

mjson_status_t mjson_skip_string(struct mjson_ctx *ctx)
{
  size_t temp;
  return mjson_read_full_string(ctx, NULL, 0, &temp);
}

mjson_status_t mjson_readcheck_array_start(struct mjson_ctx *ctx)
{
  return mjson_read_type(ctx) == MJSON_TYPE_ARRAY ?
      MJSON_OK : MJSON_ERROR_CHECK_FAILURE;
}

mjson_status_t mjson_read_array_separator_or_end(struct mjson_ctx *ctx)
{
  mjson_status_t status;
  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }

  if (*MJSON_BUFFER_START_(ctx) == ',') {
    mjson_consume_byte_(ctx, 1);
    return MJSON_SUBTYPE_ARRAY_SEPARATOR;
  } else if (*MJSON_BUFFER_START_(ctx) == ']') {
    mjson_consume_byte_(ctx, 1);
    return MJSON_SUBTYPE_ARRAY_END;
  }
  return MJSON_ERROR_TEST_NOT_TRUE;
}

mjson_status_t mjson_skip_array(struct mjson_ctx *ctx)
{
  mjson_status_t status;

  while (mjson_read_array_separator_or_end(ctx) != MJSON_SUBTYPE_ARRAY_END) {
    status = mjson_skip_value(ctx);
    if (status != MJSON_OK) { return status; }
  }

  return MJSON_OK;
}

mjson_status_t mjson_readcheck_object_start(struct mjson_ctx *ctx)
{
  return mjson_read_type(ctx) == MJSON_TYPE_OBJECT ?
      MJSON_OK : MJSON_ERROR_CHECK_FAILURE;
}

mjson_status_t mjson_read_object_key_separator(struct mjson_ctx *ctx)
{
  return mjson_read_byte_(ctx, ':');
}

mjson_status_t mjson_read_object_separator_or_end(struct mjson_ctx *ctx)
{
  mjson_status_t status;
  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }

  if (*MJSON_BUFFER_START_(ctx) == ',') {
    mjson_consume_byte_(ctx, 1);
    return MJSON_SUBTYPE_OBJECT_SEPARATOR;
  } else if (*MJSON_BUFFER_START_(ctx) == '}') {
    mjson_consume_byte_(ctx, 1);
    return MJSON_SUBTYPE_OBJECT_END;
  }
  return MJSON_ERROR_TEST_NOT_TRUE;
}

mjson_status_t mjson_skip_object(struct mjson_ctx *ctx)
{
  mjson_status_t status;

  while (mjson_read_object_separator_or_end(ctx) != MJSON_SUBTYPE_OBJECT_END) {
    status = mjson_readcheck_string_start(ctx);
    if (status != MJSON_OK) { return status; }

    status = mjson_skip_string(ctx);
    if (status != MJSON_OK) { return status; }

    status = mjson_read_object_key_separator(ctx);
    if (status != MJSON_OK) { return status; }

    status = mjson_skip_value(ctx);
    if (status != MJSON_OK) { return status; }
  }

  return MJSON_OK;
}

mjson_status_t mjson_read_int8(struct mjson_ctx *ctx, int8_t *out)
{
  int8_t val = 0, sign = 1;
  mjson_status_t status;

  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }
  if (MJSON_BUFFER_START_(ctx)[0] == '-') {
    mjson_consume_byte_(ctx, 1);
    sign = -1;
  }

  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }
  while (MJSON_BUFFER_START_(ctx)[0] >= '0' &&
         MJSON_BUFFER_START_(ctx)[0] <= '9') {
    val = val * 10 + (MJSON_BUFFER_START_(ctx)[0] - '0');

    mjson_consume_byte_(ctx, 1);
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }
  }
  *out = val * sign;
  /* Skip additional parts */
  while ((MJSON_BUFFER_START_(ctx)[0] >= '0' &&
          MJSON_BUFFER_START_(ctx)[0] <= '9') ||
         MJSON_BUFFER_START_(ctx)[0] == '.' ||
         MJSON_BUFFER_START_(ctx)[0] == 'e' ||
         MJSON_BUFFER_START_(ctx)[0] == 'E' ||
         MJSON_BUFFER_START_(ctx)[0] == '+' ||
         MJSON_BUFFER_START_(ctx)[0] == '-') {
    mjson_consume_byte_(ctx, 1);
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }
  }
  return MJSON_OK;
}

mjson_status_t mjson_read_int16(struct mjson_ctx *ctx, int16_t *out)
{
  int16_t val = 0, sign = 1;
  mjson_status_t status;

  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }
  if (MJSON_BUFFER_START_(ctx)[0] == '-') {
    mjson_consume_byte_(ctx, 1);
    sign = -1;
  }

  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }
  while (MJSON_BUFFER_START_(ctx)[0] >= '0' &&
         MJSON_BUFFER_START_(ctx)[0] <= '9') {
    val = val * 10 + (MJSON_BUFFER_START_(ctx)[0] - '0');

    mjson_consume_byte_(ctx, 1);
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }
  }
  *out = val * sign;
  /* Skip additional parts */
  while ((MJSON_BUFFER_START_(ctx)[0] >= '0' &&
          MJSON_BUFFER_START_(ctx)[0] <= '9') ||
         MJSON_BUFFER_START_(ctx)[0] == '.' ||
         MJSON_BUFFER_START_(ctx)[0] == 'e' ||
         MJSON_BUFFER_START_(ctx)[0] == 'E' ||
         MJSON_BUFFER_START_(ctx)[0] == '+' ||
         MJSON_BUFFER_START_(ctx)[0] == '-') {
    mjson_consume_byte_(ctx, 1);
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }
  }
  return MJSON_OK;
}

mjson_status_t mjson_read_int32(struct mjson_ctx *ctx, int32_t *out)
{
  int32_t val = 0, sign = 1;
  mjson_status_t status;

  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }
  if (MJSON_BUFFER_START_(ctx)[0] == '-') {
    mjson_consume_byte_(ctx, 1);
    sign = -1;
  }

  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }
  while (MJSON_BUFFER_START_(ctx)[0] >= '0' &&
         MJSON_BUFFER_START_(ctx)[0] <= '9') {
    val = val * 10 + (MJSON_BUFFER_START_(ctx)[0] - '0');

    mjson_consume_byte_(ctx, 1);
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }
  }
  *out = val * sign;
  /* Skip additional parts */
  while ((MJSON_BUFFER_START_(ctx)[0] >= '0' &&
          MJSON_BUFFER_START_(ctx)[0] <= '9') ||
         MJSON_BUFFER_START_(ctx)[0] == '.' ||
         MJSON_BUFFER_START_(ctx)[0] == 'e' ||
         MJSON_BUFFER_START_(ctx)[0] == 'E' ||
         MJSON_BUFFER_START_(ctx)[0] == '+' ||
         MJSON_BUFFER_START_(ctx)[0] == '-') {
    mjson_consume_byte_(ctx, 1);
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }
  }
  return MJSON_OK;
}

mjson_status_t mjson_read_int64(struct mjson_ctx *ctx, int64_t *out)
{
  int64_t val = 0, sign = 1;
  mjson_status_t status;

  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }
  if (MJSON_BUFFER_START_(ctx)[0] == '-') {
    mjson_consume_byte_(ctx, 1);
    sign = -1;
  }

  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }
  while (MJSON_BUFFER_START_(ctx)[0] >= '0' &&
         MJSON_BUFFER_START_(ctx)[0] <= '9') {
    val = val * 10 + (MJSON_BUFFER_START_(ctx)[0] - '0');

    mjson_consume_byte_(ctx, 1);
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }
  }
  *out = val * sign;
  /* Skip additional parts */
  while ((MJSON_BUFFER_START_(ctx)[0] >= '0' &&
          MJSON_BUFFER_START_(ctx)[0] <= '9') ||
         MJSON_BUFFER_START_(ctx)[0] == '.' ||
         MJSON_BUFFER_START_(ctx)[0] == 'e' ||
         MJSON_BUFFER_START_(ctx)[0] == 'E' ||
         MJSON_BUFFER_START_(ctx)[0] == '+' ||
         MJSON_BUFFER_START_(ctx)[0] == '-') {
    mjson_consume_byte_(ctx, 1);
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }
  }
  return MJSON_OK;
}
/*
 * We don't really support parsing to double directly here, since to implement
 * full double parsing semantics, we would need pow(). However, if pow() is
 * available, it is very likely that atof() is also available. Hence providing
 * copying number as a string will be enough.
 */
mjson_status_t mjson_read_number_as_string(struct mjson_ctx *ctx,
                                           char *data, size_t length,
                                           size_t *out_length) {
  mjson_status_t status;
  size_t i = 0;
  char ch;

  while (i < length) {
    status = mjson_ensure_byte_(ctx, 1);
    if (status != MJSON_OK) { return status; }

    ch = MJSON_BUFFER_START_(ctx)[0];
    if ((ch >= '0' && ch <= '9') ||
        ch == '.' ||
        ch == '+' || ch == '-' ||
        ch == 'e' || ch == 'E') {
      data[i++] = ch;
      mjson_consume_byte_(ctx, 1);
    } else {
      break;
    }
  }
  status = mjson_ensure_byte_(ctx, 1);
  if (status != MJSON_OK) { return status; }
  ch = MJSON_BUFFER_START_(ctx)[0];
  while ((ch >= '0' && ch <= '9') ||
      ch == '.' ||
      ch == '+' || ch == '-' ||
      ch == 'e' || ch == 'E') {
    i++;
    mjson_consume_byte_(ctx, 1);
  }
  *out_length = i;
  return MJSON_OK;
}

mjson_status_t mjson_skip_number(struct mjson_ctx *ctx)
{
  size_t temp;
  return mjson_read_number_as_string(ctx, NULL, 0, &temp);
}

mjson_status_t mjson_skip_value(struct mjson_ctx *ctx) {
  mjson_status_t t = mjson_read_type(ctx);
  switch (t) {
    case MJSON_TYPE_ARRAY:
      return mjson_skip_array(ctx);
    case MJSON_TYPE_OBJECT:
      return mjson_skip_object(ctx);
    case MJSON_TYPE_NUMBER:
      return mjson_skip_number(ctx);
    case MJSON_TYPE_STRING:
      return mjson_skip_string(ctx);
    case MJSON_TYPE_NULL:
    case MJSON_TYPE_TRUE:
    case MJSON_TYPE_FALSE:
      return MJSON_OK;
  }
  return t;
}

#endif  /* MJSON_H_ */

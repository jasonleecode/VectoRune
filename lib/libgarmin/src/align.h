#ifndef LIBGARMIN_ALIGN_H
#define LIBGARMIN_ALIGN_H

/* IMG integers are little-endian and may be unaligned. Functions also avoid
 * the old macros' precedence bug for arguments such as get_u24(pointer + 2).
 */
static inline unsigned int get_u32(const void *ptr)
{
    const unsigned char *p = (const unsigned char *)ptr;
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static inline unsigned int get_u24(const void *ptr)
{
    const unsigned char *p = (const unsigned char *)ptr;
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16);
}
static inline unsigned short get_u16(const void *ptr)
{
    const unsigned char *p = (const unsigned char *)ptr;
    return (unsigned short)((unsigned int)p[0] | ((unsigned int)p[1] << 8));
}
static inline unsigned char get_u8(const void *ptr)
{
    return *(const unsigned char *)ptr;
}
#endif

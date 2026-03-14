// SPDX-License-Identifier: GPL-2.0
/*
 * Provide access to virtual console memory.
 * /dev/vcs0: the screen as it is being viewed right now (possibly scrolled)
 * /dev/vcsN: the screen of /dev/ttyN (1 <= N <= 63)
 *            [minor: N]
 *
 * /dev/vcsaN: idem, but including attributes, and prefixed with
 *      the 4 bytes lines,columns,x,y (as screendump used to give).
 *      Attribute/character pair is in native endianity.
 *            [minor: N+128]
 *
 * /dev/vcsuN: similar to /dev/vcsaN but using 4-byte unicode values
 *      instead of 1-byte screen glyph values.
 *            [minor: N+64]
 *
 * /dev/vcsuaN: same idea as /dev/vcsaN for unicode (not yet implemented).
 *
 * This replaces screendump and part of selection, so that the system
 * administrator can control access using file system permissions.
 *
 * aeb@cwi.nl - efter Friedas begravelse - 950211
 *
 * machek@k332.feld.cvut.cz - modified not to send characters to wrong console
 *       - fixed some fatal off-by-one bugs (0-- no longer == -1 -> looping and looping and looping...)
 *       - making it shorter - scr_readw are macros which expand in PRETTY long code
 */

#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/tty.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/kbd_kern.h>
#include <linux/console.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/notifier.h>

#include <linux/uaccess.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#undef attr
#undef org
#undef addr
#define HEADER_SIZE     4

#define CON_BUF_SIZE (CONFIG_BASE_SMALL ? 256 : PAGE_SIZE)

/*
 * Our minor space:
 *
 *   0 ... 63   glyph mode without attributes
 *  64 ... 127  unicode mode without attributes
 * 128 ... 191  glyph mode with attributes
 * 192 ... 255  unused (reserved for unicode with attributes)
 *
 * This relies on MAX_NR_CONSOLES being  <= 63, meaning 63 actual consoles
 * with minors 0, 64, 128 and 192 being proxies for the foreground console.
 */
#if MAX_NR_CONSOLES > 63
#warning "/dev/vcs* devices may not accommodate more than 63 consoles"
#endif

#define console(inode)          (iminor(inode) & 63)
#define use_unicode(inode)      (iminor(inode) & 64)
#define use_attributes(inode)   (iminor(inode) & 128)

static ssize_t
vcs_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
        struct inode *inode = file_inode(file);
        struct vc_data *vc;
        long pos;
        long attr, size, written;
        char *con_buf0;
        int col, maxcol, viewed;
        u16 *org0 = NULL, *org = NULL;
        size_t ret;
        char *con_buf;

        con_buf = (char *) __get_free_page(GFP_KERNEL);
        if (!con_buf)
                return -ENOMEM;

        pos = *ppos;

        /* Select the proper current console and verify
         * sanity of the situation under the console lock.
         */
        console_lock();

        attr = use_attributes(inode);
        ret = -ENXIO;
        vc = vcs_vc(inode, &viewed);
        if (!vc)
                goto unlock_out;

        size = vcs_size(inode);
        ret = -EINVAL;
        if (pos < 0 || pos > size)
                goto unlock_out;
        if (count > size - pos)
                count = size - pos;
        written = 0;
        while (count) {
                long this_round = count;
                size_t orig_count;
                long p;

                if (this_round > CON_BUF_SIZE)
                        this_round = CON_BUF_SIZE;

                /* Temporarily drop the console lock so that we can read
                * in the write data from userspace safely.
                 */
                console_unlock();
                ret = copy_from_user(con_buf, buf, this_round);
                console_lock();

                if (ret) {
                        this_round -= ret;
                        if (!this_round) {
                                /* Abort loop if no data were copied. Otherwise
                                 * fail with -EFAULT.
                                 */
                                if (written)
                                        break;
                                ret = -EFAULT;
                                goto unlock_out;
                        }
                }

                /* The vcs_size might have changed while we slept to grab
                 * the user buffer, so recheck.
                 * Return data written up to now on failure.
                 */
                size = vcs_size(inode);
                if (size < 0) {
                        if (written)
                                break;
                        ret = size;
                        goto unlock_out;
                }
                if (pos >= size)
                        break;
                if (this_round > size - pos)
                        this_round = size - pos;

                /* OK, now actually push the write to the console
                 * under the lock using the local kernel buffer.
                 */

                con_buf0 = con_buf;
                orig_count = this_round;
                maxcol = vc->vc_cols;
                p = pos;
                if (!attr) {
                        org0 = org = screen_pos(vc, p, viewed);
                        col = p % maxcol;
                        p += maxcol - col;

                        while (this_round > 0) {
                                unsigned char c = *con_buf0++;

                                this_round--;

                                /* [RC TRACER] Check that org is still within
                                 * the screen buffer before reading. A violation
                                 * here indicates a concurrent resize occurred
                                 * during the console_unlock window above,
                                 * confirming the race condition hypothesis.
                                 */
                                WARN_ON(org >= (u16 *)vc->vc_origin +
                                        vc->vc_screenbuf_size / 2);

                                vcs_scr_writew(vc,
                                               (vcs_scr_readw(vc, org) & 0xff00) | c, org);
                                org++;
                                if (++col == maxcol) {
                                        org = screen_pos(vc, p, viewed);
                                        col = 0;
                                        p += maxcol;
                                }
                        }
                } else {
                        if (p < HEADER_SIZE) {
                                char header[HEADER_SIZE];

                                getconsxy(vc, header + 2);
                                while (p < HEADER_SIZE && this_round > 0) {
                                        this_round--;
                                        header[p++] = *con_buf0++;
                                }
                                if (!viewed)
                                        putconsxy(vc, header + 2);
                        }
                        p -= HEADER_SIZE;
                        col = (p/2) % maxcol;
                        if (this_round > 0) {
                                org0 = org = screen_pos(vc, p/2, viewed);

                                /* [PATCH] Revalidate org after reacquiring the
                                 * console lock. The buffer may have been resized
                                 * by a concurrent vc_do_resize call during the
                                 * console_unlock window, invalidating org.
                                 * Recompute to ensure it points within the
                                 * current buffer bounds.
                                 */
                                size = vcs_size(inode);
                                if (size < 0 || pos >= size)
                                        break;
                                org0 = org = screen_pos(vc, p/2, viewed);

                                if ((p & 1) && this_round > 0) {
                                        char c;

                                        this_round--;
                                        c = *con_buf0++;
#ifdef __BIG_ENDIAN
                                        vcs_scr_writew(vc, c |
                                             (vcs_scr_readw(vc, org) & 0xff00), org);
#else
                                        vcs_scr_writew(vc, (c << 8) |
                                             (vcs_scr_readw(vc, org) & 0xff), org);
#endif
                                        org++;
                                        p++;
                                        if (++col == maxcol) {
                                                org = screen_pos(vc, p/2, viewed);
                                                col = 0;
                                        }
                                }
                                p /= 2;
                                p += maxcol - col;
                        }
                        while (this_round > 1) {
                                unsigned short w;

                                w = get_unaligned(((unsigned short *)con_buf0));
                                vcs_scr_writew(vc, w, org++);
                                con_buf0 += 2;
                                this_round -= 2;
                                if (++col == maxcol) {
                                        org = screen_pos(vc, p, viewed);
                                        col = 0;
                                        p += maxcol;
                                }
                        }
                        if (this_round > 0) {
                                unsigned char c;

                                c = *con_buf0++;
#ifdef __BIG_ENDIAN
                                vcs_scr_writew(vc, (vcs_scr_readw(vc, org) & 0xff) | (c << 8), org);
#else
                                vcs_scr_writew(vc, (vcs_scr_readw(vc, org) & 0xff00) | c, org);
#endif
                        }
                }
                count -= orig_count;
                written += orig_count;
                buf += orig_count;
                pos += orig_count;
                if (org0)
                        update_region(vc, (unsigned long)(org0), org - org0);
        }
        *ppos += written;
        ret = written;
        if (written)
                vcs_scr_updated(vc);

unlock_out:
        console_unlock();
        free_page((unsigned long) con_buf);
        return ret;
}
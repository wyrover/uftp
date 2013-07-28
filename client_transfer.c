/*
 *  UFTP - UDP based FTP with multicast
 *
 *  Copyright (C) 2001-2013   Dennis A. Bush, Jr.   bush@tcnj.edu
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Additional permission under GNU GPL version 3 section 7
 *
 *  If you modify this program, or any covered work, by linking or
 *  combining it with the OpenSSL project's OpenSSL library (or a
 *  modified version of that library), containing parts covered by the
 *  terms of the OpenSSL or SSLeay licenses, the copyright holder
 *  grants you additional permission to convey the resulting work.
 *  Corresponding Source for a non-source form of such a combination
 *  shall include the source code for the parts of OpenSSL used as well
 *  as that of the covered work.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>

#ifdef WINDOWS

#include <io.h>
#include <direct.h>

#include "win_func.h"

#else  // if WINDOWS

#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#endif

#include "client.h"
#include "client_common.h"
#include "client_transfer.h"

/**
 * Move all files from temp to destination directory if at end of group
 * TODO: add option to move files one at a time?
 */
void move_files(struct group_list_t *group)
{
    char temppath[MAXPATHNAME], destpath[MAXPATHNAME];
    char *filelist[10000];  // TODO: no magic number
    int len, filecount, i;

    if (!strcmp(tempdir, "") || (group->file_id != 0)) {
        return;
    }

    {
#ifdef WINDOWS
        intptr_t ffhandle;
        struct _finddatai64_t finfo;
        char dirglob[MAXPATHNAME];

        snprintf(dirglob, sizeof(dirglob), "%s%c_group_%08X%c*", tempdir,
                 PATH_SEP, group->group_id, PATH_SEP);
        if ((ffhandle = _findfirsti64(dirglob, &finfo)) == -1) {
            syserror(group->group_id, group->file_id,
                     "Failed to open directory %s", dirglob);
            return;
        }
        filecount = 0;
        do {
            len = snprintf(temppath, sizeof(temppath), "%s%c_group_%08X%c%s",
                           tempdir, PATH_SEP, group->group_id,
                           PATH_SEP, finfo.name);
            if ((len >= sizeof(temppath)) || (len == -1)) {
                log0(group->group_id, group->file_id,
                        "Max pathname length exceeded: %s%c_group_%08X%c%s",
                        tempdir, PATH_SEP, group->group_id,
                        PATH_SEP, finfo.name);
                continue;
            }
            len = snprintf(destpath, sizeof(destpath), "%s%c%s",
                           destdir[0], PATH_SEP, finfo.name);
            if ((len >= sizeof(destpath)) || (len == -1)) {
                log0(group->group_id, group->file_id,
                        "Max pathname length exceeded: %s%c%s",
                        destdir[0], PATH_SEP, finfo.name);
                continue;
            }
            // do the move
            if (strcmp(finfo.name, ".") && strcmp(finfo.name, "..")) {
                clear_path(destpath, group);
                if (!MoveFile(temppath, destpath)) {
                    char errbuf[300];
                    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL,
                            GetLastError(), 0, errbuf, sizeof(errbuf), NULL);
                    log0(group->group_id, group->file_id,
                            "error (%d): %s", GetLastError(), errbuf);
                }
                filelist[filecount] = strdup(destpath);
                if (filelist[filecount] == NULL) {
                    syserror(0, 0, "strdup failed!");
                    exit(1);
                }
                filecount++;
            }
        } while (_findnexti64(ffhandle, &finfo) == 0);
        _findclose(ffhandle);
#else
        DIR *dir;
        struct dirent *de;
        char dirname[MAXPATHNAME];

        snprintf(dirname, sizeof(dirname), "%s%c_group_%08X", tempdir,
                 PATH_SEP, group->group_id);
        if ((dir = opendir(dirname)) == NULL) {
            syserror(group->group_id, group->file_id,
                     "Failed to open directory %s", dirname);
            return;
        }
        filecount = 0;
        // errno needs to be set to 0 before calling readdir, otherwise
        // we'll report a false error when we exhaust the directory
        while ((errno = 0, de = readdir(dir)) != NULL) {
            len = snprintf(temppath, sizeof(temppath), "%s%c%s", dirname,
                           PATH_SEP, de->d_name);
            if ((len >= sizeof(temppath)) || (len == -1)) {
                log0(group->group_id, group->file_id,
                        "Max pathname length exceeded: %s%c%s", dirname,
                        PATH_SEP, de->d_name);
                continue;
            }
            len = snprintf(destpath, sizeof(destpath), "%s%c%s", destdir[0],
                           PATH_SEP, de->d_name);
            if ((len >= sizeof(destpath)) || (len == -1)) {
                log0(group->group_id, group->file_id,
                        "Max pathname length exceeded: %s%c%s", destdir[0],
                        PATH_SEP, de->d_name);
                continue;
            }
            if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
                clear_path(destpath, group);
                if (rename(temppath, destpath) == -1) {
                    syserror(group->group_id,
                             group->file_id, "Couldn't move file");
                }
                filelist[filecount] = strdup(destpath);
                if (filelist[filecount] == NULL) {
                    syserror(0, 0, "strdup failed!");
                    exit(1);
                }
                filecount++;
            }
        }
        if (errno && (errno != ENOENT)) {
            syserror(group->group_id, group->file_id,
                     "Failed to read directory %s", dirname);
        }
        closedir(dir);
#endif
    }
    run_postreceive_multi(group, filelist, filecount);
    for (i = 0; i < filecount; i++) {
        free(filelist[i]);
    }
    snprintf(temppath, sizeof(temppath), "%s%c_group_%08X", tempdir,
             PATH_SEP, group->group_id);
    if (rmdir(temppath) == -1) {
        syserror(group->group_id, group->file_id,
                 "Failed remove temp directory %s", temppath);
    }
}

/**
 * Sets the fields in a EXT_TFMCC_ACK_INFO extension for transmission
 */
void set_tfmcc_ack_info(struct group_list_t *group, 
                        struct tfmcc_ack_info_he *tfmcc)
{
    struct timeval now, send_time;
    unsigned ccrate;

    tfmcc->exttype = EXT_TFMCC_ACK_INFO;
    tfmcc->extlen = sizeof(struct tfmcc_ack_info_he) / 4;
    tfmcc->cc_seq = htons(group->ccseq);
    ccrate = current_cc_rate(group);
    log4(group->group_id, 0, "ccrate=%d", ccrate);
    tfmcc->cc_rate = htons(quantize_rate(ccrate));
    //tfmcc->cc_rate = htons(quantize_rate(current_cc_rate(group)));
    tfmcc->flags = 0;
    if (group->slowstart) {
        tfmcc->flags |= FLAG_CC_START;
    }
    if (group->rtt != 0.0) {
        tfmcc->flags |= FLAG_CC_RTT;
    }
    tfmcc->client_id = uid;

    gettimeofday(&now, NULL);
    if (cmptimestamp(now, group->last_server_rx_ts) <= 0) {
        send_time = group->last_server_ts;
    } else {
        send_time = add_timeval(group->last_server_ts,
                diff_timeval(now, group->last_server_rx_ts));
    }
    tfmcc->tstamp_sec = htonl((uint32_t)send_time.tv_sec);
    tfmcc->tstamp_usec = htonl((uint32_t)send_time.tv_usec);
}

/**
 * Sends back a STATUS message with the given NAK list
 */
void send_status(struct group_list_t *group, unsigned int section,
                 const unsigned char *naks, unsigned int nak_count)
{
    unsigned char *buf, *encrypted, *outpacket;
    struct uftp_h *header;
    struct status_h *status;
    struct tfmcc_ack_info_he *tfmcc;
    unsigned char *sent_naks;
    int payloadlen, enclen;

    buf = calloc(MAXMTU, 1);
    if (buf == NULL) {
        syserror(0, 0, "calloc failed!");
        exit(1);
    }

    header = (struct uftp_h *)buf;
    status = (struct status_h *)(buf + sizeof(struct uftp_h));
    tfmcc = (struct tfmcc_ack_info_he *)((unsigned char *)status +
                sizeof(struct status_h));

    set_uftp_header(header, STATUS, group);
    status->func = STATUS;
    if (group->cc_type == CC_TFMCC) {
        status->hlen =
              (sizeof(struct status_h) + sizeof(struct tfmcc_ack_info_he)) / 4;
    } else {
        status->hlen = sizeof(struct status_h) / 4;
    }
    status->file_id = htons(group->file_id);
    status->section = htons(section);
    if (section >= group->fileinfo.big_sections) {
        payloadlen = (group->fileinfo.secsize_small / 8) + 1;
    } else {
        payloadlen = (group->fileinfo.secsize_big / 8) + 1;
    }
    if (group->cc_type == CC_TFMCC) {
        set_tfmcc_ack_info(group, tfmcc);
    }
    sent_naks = (unsigned char *)status + (status->hlen * 4);
    memcpy(sent_naks, naks, payloadlen);

    payloadlen += status->hlen * 4;
    if ((group->phase != PHASE_REGISTERED) && (group->keytype != KEY_NONE)) {
        encrypted = NULL;
        if (!encrypt_and_sign(buf, &encrypted, payloadlen, &enclen,
                group->keytype, group->groupkey, group->groupsalt,&group->ivctr,
                group->ivlen, group->hashtype, group->grouphmackey,
                group->hmaclen, group->sigtype, group->keyextype,
                group->client_privkey, group->client_privkeylen)) {
            log0(group->group_id, group->file_id, "Error encrypting STATUS");
            free(buf);
            return;
        }
        outpacket = encrypted;
        payloadlen = enclen;
    } else {
        encrypted = NULL;
        outpacket = buf;
    }
    payloadlen += sizeof(struct uftp_h);

    if (nb_sendto(listener, outpacket, payloadlen, 0,
               (struct sockaddr *)&group->replyaddr,
               family_len(group->replyaddr)) == SOCKET_ERROR) {
        sockerror(group->group_id, group->file_id, "Error sending STATUS");
    } else {
        log2(group->group_id, group->file_id,
                "Sent %d NAKs for section %d", nak_count, section);
    }

    free(buf);
    free(encrypted);
}

/**
 * Sends back a COMPLETE message in response to a DONE or FILEINFO
 */
void send_complete(struct group_list_t *group)
{
    unsigned char *buf, *encrypted, *outpacket;
    struct uftp_h *header;
    struct complete_h *complete;
    int payloadlen, enclen;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    if ((group->phase == PHASE_COMPLETE) &&
            (cmptimestamp(tv, group->expire_time) >= 0)) {
        log0(group->group_id, group->file_id,
                "Completion unconfirmed by server");
        move_files(group);
        file_cleanup(group, 0);
        return;
    }
    buf = calloc(MAXMTU, 1);
    if (buf == NULL) {
        syserror(0, 0, "calloc failed!");
        exit(1);
    }

    header = (struct uftp_h *)buf;
    complete = (struct complete_h *)(buf + sizeof(struct uftp_h));

    set_uftp_header(header, COMPLETE, group);
    complete->func = COMPLETE;
    complete->hlen = sizeof(struct complete_h) / 4;
    complete->status = group->fileinfo.comp_status;
    complete->file_id = htons(group->file_id);

    payloadlen = sizeof(struct complete_h);
    if ((group->phase != PHASE_REGISTERED) && (group->keytype != KEY_NONE)) {
        encrypted = NULL;
        if (!encrypt_and_sign(buf, &encrypted, payloadlen, &enclen,
                group->keytype, group->groupkey, group->groupsalt,&group->ivctr,
                group->ivlen, group->hashtype, group->grouphmackey,
                group->hmaclen, group->sigtype, group->keyextype,
                group->client_privkey, group->client_privkeylen)) {
            log0(group->group_id, group->file_id, "Error encrypting COMPLETE");
            free(buf);
            return;
        }
        outpacket = encrypted;
        payloadlen = enclen;
    } else {
        encrypted = NULL;
        outpacket = buf;
    }
    payloadlen += sizeof(struct uftp_h);

    if (nb_sendto(listener, outpacket, payloadlen, 0,
               (struct sockaddr *)&group->replyaddr,
               family_len(group->replyaddr)) == SOCKET_ERROR) {
        sockerror(group->group_id, group->file_id, "Error sending COMPLETE");
    } else {
        log2(group->group_id, group->file_id, "COMPLETE sent");
    }
    set_timeout(group, 0);

    free(buf);
    free(encrypted);
}

/**
 * Sends back a CC_ACK message for congestion control feedback
 */
void send_cc_ack(struct group_list_t *group)
{
    unsigned char *buf, *encrypted, *outpacket;
    struct uftp_h *header;
    struct cc_ack_h *cc_ack;
    struct tfmcc_ack_info_he *tfmcc;
    int payloadlen, enclen;

    buf = calloc(MAXMTU, 1);
    if (buf == NULL) {
        syserror(0, 0, "calloc failed!");
        exit(1);
    }

    header = (struct uftp_h *)buf;
    cc_ack = (struct cc_ack_h *)(buf + sizeof(struct uftp_h));
    tfmcc = (struct tfmcc_ack_info_he *)((unsigned char *)cc_ack +
                sizeof(struct cc_ack_h));

    set_uftp_header(header, CC_ACK, group);
    cc_ack->func = CC_ACK;
    cc_ack->hlen =
            (sizeof(struct cc_ack_h) + sizeof(struct tfmcc_ack_info_he)) / 4;
    set_tfmcc_ack_info(group, tfmcc);

    payloadlen = cc_ack->hlen * 4;
    if ((group->phase != PHASE_REGISTERED) && (group->keytype != KEY_NONE)) {
        encrypted = NULL;
        if (!encrypt_and_sign(buf, &encrypted, payloadlen, &enclen,
                group->keytype, group->groupkey, group->groupsalt,&group->ivctr,
                group->ivlen, group->hashtype, group->grouphmackey,
                group->hmaclen, group->sigtype, group->keyextype,
                group->client_privkey, group->client_privkeylen)) {
            log0(group->group_id, group->file_id, "Error encrypting CC_ACK");
            free(buf);
            return;
        }
        outpacket = encrypted;
        payloadlen = enclen;
    } else {
        encrypted = NULL;
        outpacket = buf;
    }
    payloadlen += sizeof(struct uftp_h);

    if (nb_sendto(listener, outpacket, payloadlen, 0,
               (struct sockaddr *)&group->replyaddr,
               family_len(group->replyaddr)) == SOCKET_ERROR) {
        sockerror(group->group_id, group->file_id, "Error sending CC_ACK");
    } else {
        log2(group->group_id, group->file_id, "CC_ACK sent");
    }
    set_timeout(group, 0);
    group->cc_time.tv_sec = 0;
    group->cc_time.tv_usec = 0;
    free(buf);
}

/**
 * Starts a new feedback round under TFMCC
 */
void init_tfmcc_fb_round(struct group_list_t *group, uint16_t new_ccseq)
{
    double urand, backoff;
    group->ccseq = new_ccseq;

    urand = (double)rand32() / 0xFFFFFFFF;
    if (urand == 0.0) urand = 1.0;
    backoff = 6 * group->grtt * (1 + log(urand) / log(group->gsize));
    if (backoff < 0) backoff = 0.0;
    gettimeofday(&group->cc_time, NULL);
    add_timeval_d(&group->cc_time, (backoff > 0) ? backoff : 0);
    group->initrate = current_cc_rate(group);
    log3(group->group_id, group->file_id, "Starting feedback round %d: "
            "backoff = %.3f, initrate = %d",
            new_ccseq, backoff, group->initrate);
}

/**
 * Reads a EXT_TFMCC_DATA_INFO extension in a FILESEG message
 */
void handle_tfmcc_data_info(struct group_list_t *group,
                            struct tfmcc_data_info_he *tfmcc)
{
    uint32_t ccrate;
    uint16_t new_ccseq;

    new_ccseq = ntohs(tfmcc->cc_seq);
    ccrate = unquantize_rate(ntohs(tfmcc->cc_rate));
    if (((int16_t)(new_ccseq - group->ccseq)) > 0) {
        init_tfmcc_fb_round(group, new_ccseq);
    } else if ((group->cc_time.tv_sec != 0) &&
            ((ccrate < current_cc_rate(group)) || (ccrate < group->initrate)) &&
            (group->grtt > group->rtt)) {
        log4(group->group_id, group->file_id, "Canceling feedback timer");
        group->cc_time.tv_sec = 0;
        group->cc_time.tv_usec = 0;
    }
}

/**
 * Reads an expected FILESEG and writes it to the proper place in the file
 */
void handle_fileseg(struct group_list_t *group, const unsigned char *message,
                    unsigned meslen, uint16_t txseq)
{
    struct fileseg_h *fileseg;
    struct tfmcc_data_info_he *tfmcc;
    const unsigned char *data;
    uint8_t *he;
    int datalen, section, seq, wrote_len;
    unsigned extlen;
    f_offset_t offset, seek_rval;

    if (group->fileinfo.ftype != FTYPE_REG) {
        log2(group->group_id, group->file_id,
                "Rejecting FILESEG: not a regular file");
        return;
    }
    fileseg = (struct fileseg_h *)message;
    data = message + (fileseg->hlen * 4);
    datalen = meslen - (fileseg->hlen * 4);

    if ((meslen < (fileseg->hlen * 4U)) ||
            ((fileseg->hlen * 4U) < sizeof(struct fileseg_h))) {
        log2(group->group_id, group->file_id,
                "Rejecting FILESEG: invalid message size");
        return;
    }
    if (ntohs(fileseg->file_id) != group->file_id) {
        log2(group->group_id, group->file_id,
                "Rejecting FILESEG: got incorrect file_id %04X",
                ntohs(fileseg->file_id));
        return;
    }

    tfmcc = NULL;
    if (fileseg->hlen * 4U > sizeof(struct fileseg_h)) {
        he = (uint8_t *)fileseg + sizeof(struct fileseg_h);
        if (*he == EXT_TFMCC_DATA_INFO) {
            tfmcc = (struct tfmcc_data_info_he *)he;
            extlen = tfmcc->extlen * 4U;
            if ((extlen > (fileseg->hlen * 4U) - sizeof(struct fileseg_h)) ||
                    extlen < sizeof(struct tfmcc_data_info_he)) {
                log2(group->group_id, group->file_id,
                        "Rejecting FILESEG: invalid extension size");
                return;
            }
        }
    }

    section = ntohs(fileseg->section);
    if (section >= group->fileinfo.big_sections) {
        seq = (group->fileinfo.big_sections * group->fileinfo.secsize_big) +
                ((section - group->fileinfo.big_sections) *
                group->fileinfo.secsize_small) + ntohs(fileseg->sec_block);
    } else {
        seq = (section * group->fileinfo.secsize_big) +
                ntohs(fileseg->sec_block);
    }

    if ((datalen != group->blocksize) &&
            (seq != group->fileinfo.blocks - 1)) {
        log2(group->group_id, group->file_id,
                "Rejecting FILESEG: invalid data size %d", datalen);
        return;
    }
    if (log_level >= 5) {
        log5(group->group_id, group->file_id, "Got packet %d", seq);
    } else if (log_level == 4) {
        if (seq != group->fileinfo.last_block + 1) {
            log4(group->group_id, group->file_id, "Got packet %d, last was %d",
                    seq, group->fileinfo.last_block);
        }
    }

    if ((group->cc_type == CC_TFMCC) && tfmcc) {
        handle_tfmcc_data_info(group, tfmcc);
    }

    group->fileinfo.last_block = seq;
    if (txseq == group->max_txseq) {
        if ((section > group->fileinfo.last_section) &&
                (group->fileinfo.nak_time.tv_sec == 0)) {
            // Start timer to send NAKs
            gettimeofday(&group->fileinfo.nak_time, NULL);
            add_timeval_d(&group->fileinfo.nak_time, 1 * group->grtt);
            group->fileinfo.nak_section = section;
        }
        group->fileinfo.last_section = section;
    }
    if (group->fileinfo.naklist[seq]) {
        offset = (f_offset_t) seq * group->blocksize;
        if ((seek_rval = lseek_func(group->fileinfo.fd,
                offset - group->fileinfo.curr_offset,
                SEEK_CUR)) == -1) {
            syserror(group->group_id, group->file_id, "lseek failed for file");
        }
        if (seek_rval != offset) {
            log2(group->group_id, group->file_id,
                    "offset is %s", printll(seek_rval));
            log2(group->group_id, group->file_id,
                    "  should be %s", printll(offset));
            if ((seek_rval = lseek_func(group->fileinfo.fd,
                    seek_rval - group->fileinfo.curr_offset,
                    SEEK_CUR)) == -1) {
                syserror(group->group_id, group->file_id,
                         "lseek failed for file");
                return;
            }
        }
        if ((wrote_len = write(group->fileinfo.fd, data, datalen)) == -1) {
            syserror(group->group_id, group->file_id,
                    "Write failed for segment %d", seq);
        } else {
            group->fileinfo.curr_offset = offset + wrote_len;
            if (wrote_len != datalen) {
                log0(group->group_id, group->file_id,
                        "Write failed for segment %d, only wrote %d bytes",
                        seq, wrote_len);
            } else {
                group->fileinfo.naklist[seq] = 0;
            }
        }
    }
    set_timeout(group, 0);
}

/**
 * Returns 1 if a file has been completely received, 0 otherwise
 */
int file_done(struct group_list_t *group, int detail)
{
    unsigned int section_offset, blocks_this_sec, section, block, nakidx;

    if ((group->phase == PHASE_MIDGROUP) || (group->file_id == 0)) {
        return 1;
    }
    for (section = 0; section < group->fileinfo.sections; section++) {
        if (!group->fileinfo.section_done[section]) {
            if (!detail) {
                return 0;
            }

            if (section >= group->fileinfo.big_sections) {
                section_offset = (group->fileinfo.big_sections *
                            group->fileinfo.secsize_big) +
                        ((section - group->fileinfo.big_sections) *
                            group->fileinfo.secsize_small);
                blocks_this_sec = group->fileinfo.secsize_small;
            } else {
                section_offset = section * group->fileinfo.secsize_big;
                blocks_this_sec = group->fileinfo.secsize_big;
            }

            for (block = 0; block < blocks_this_sec; block++) {
                nakidx = block + section_offset;
                if (group->fileinfo.naklist[nakidx]) {
                    return 0;
                }
            }
            group->fileinfo.section_done[section] = 1;
        }
    }
    return 1;
}

/**
 * Build the NAK list for a given section.  Returns the NAK count.
 */
unsigned int get_naks(struct group_list_t *group, 
                      unsigned int section, unsigned char **naks)
{
    unsigned int section_offset, blocks_this_sec, i;
    unsigned int nakidx, naklistidx, numnaks;
    
    if ((group->phase == PHASE_MIDGROUP) || (group->file_id == 0) ||
        (group->fileinfo.section_done[section])) {
        *naks = NULL;
        return 0;
    }

    if (section >= group->fileinfo.big_sections) {
        section_offset = (group->fileinfo.big_sections *
                    group->fileinfo.secsize_big) +
                ((section - group->fileinfo.big_sections) *
                    group->fileinfo.secsize_small);
        blocks_this_sec = group->fileinfo.secsize_small;
    } else {
        section_offset = section * group->fileinfo.secsize_big;
        blocks_this_sec = group->fileinfo.secsize_big;
    }
    log3(group->group_id, group->file_id,
            "getting naks: section: %d, offset: %d, blocks: %d",
            section, section_offset, blocks_this_sec);

    *naks = calloc(group->blocksize, 1);
    if (*naks == NULL) {
        syserror(0, 0, "calloc failed!");
        exit(1);
    }

    // Build NAK list
    numnaks = 0;
    for (i = 0; i < blocks_this_sec; i++) {
        nakidx = i + section_offset;
        naklistidx = i;
        if (group->fileinfo.naklist[nakidx]) {
            log4(group->group_id, group->file_id, "NAK for %d", nakidx);
            // Each bit represents a NAK; set each one we have a NAK for
            // Simplified: *naks[naklistidx / 8] |= (1 << (naklistidx % 8))
            (*naks)[naklistidx >> 3] |= (1 << (naklistidx & 7));
            numnaks++;
        }
    }

    // Highly verbose debugging -- print NAK list to send
    if (log_level >= 5) {
        for (i = 0; i < group->blocksize; i++) {
            sclog5("%02X ", (*naks)[i]);
            if (i % 25 == 24) slog5("");
        }
        slog5("");
    }

    if (numnaks == 0) {
        group->fileinfo.section_done[section] = 1;
    }
    return numnaks;
}

/**
 * Processes an expected DONE message
 */
void handle_done(struct group_list_t *group, const unsigned char *message,
                 unsigned meslen)
{
    struct done_h *done;
    uint32_t *addrlist;
    unsigned int section, listlen;

    done = (struct done_h *)message;
    addrlist = (uint32_t *)(message + (done->hlen * 4));
    listlen = meslen - (done->hlen * 4);

    if ((meslen < (done->hlen * 4U)) ||
            ((done->hlen * 4U) < sizeof(struct done_h))) {
        log2(group->group_id, group->file_id,
                "Rejecting DONE: invalid message size");
        return;
    }

    section = ntohs(done->section);
    if ((ntohs(done->file_id) != group->file_id) &&
            (ntohs(done->file_id) != 0)) {
        // Silently reject if not for this file and not end of group
        return;
    }

    if (ntohs(done->file_id) == 0) {
        // We're at end of group, so set local file_id=0 to flag this
        group->file_id = 0;
    }

    if (group->file_id) {
        log1(group->group_id, group->file_id,
                "Got DONE message for section %d", section);
    } else {
        log1(group->group_id, group->file_id,
                "Got DONE message for group");
    }
    if (uid_in_list(addrlist, listlen)) {
        if (group->file_id == 0) {
            log1(group->group_id, 0, "Group complete");
            group->phase = PHASE_COMPLETE;
            group->fileinfo.comp_status = COMP_STAT_NORMAL;
            gettimeofday(&group->expire_time, NULL);
            if (group->robust * group->grtt < 1.0) {
                add_timeval_d(&group->expire_time, 1.0);
            } else {
                add_timeval_d(&group->expire_time, group->robust * group->grtt);
            }
            send_complete(group);
        } else {
            if (file_done(group, 1)) {
                log1(group->group_id, group->file_id, "File transfer complete");
                send_complete(group);
                file_cleanup(group, 0);
            } else if (group->fileinfo.nak_time.tv_sec == 0) {
                log4(group->group_id, group->file_id,
                        "Setting nak_time to trigger in %.6f", group->grtt);
                gettimeofday(&group->fileinfo.nak_time, NULL);
                add_timeval_d(&group->fileinfo.nak_time, 1 * group->grtt);
                group->fileinfo.nak_section = section + 1;
            }
        }
    }
    set_timeout(group, 0);
}

/**
 * Processes an expected DONE_CONF message
 */
void handle_done_conf(struct group_list_t *group, const unsigned char *message,
                      unsigned meslen)
{
    struct doneconf_h *doneconf;
    uint32_t *addrlist;
    int listlen;

    doneconf = (struct doneconf_h *)message;
    addrlist = (uint32_t *)(message + sizeof(struct doneconf_h));
    listlen = meslen - (doneconf->hlen * 4);

    if ((meslen < (doneconf->hlen * 4U)) ||
            ((doneconf->hlen * 4U) < sizeof(struct doneconf_h))) {
        log2(group->group_id, group->file_id,
                "Rejecting DONE_CONF: invalid message size");
        return;
    }

    if (uid_in_list(addrlist, listlen)) {
        log0(group->group_id, group->file_id,
                "Group file transfer confirmed");
        move_files(group);
        file_cleanup(group, 0);
    }
}

/**
 * Processes an expected CONG_CTRL message
 */
void handle_cong_ctrl(struct group_list_t *group, const unsigned char *message,
                      unsigned meslen, struct timeval rxtime)
{
    struct cong_ctrl_h *cong_ctrl;
    struct cc_item *cc_list;
    int listlen, i, ccidx, clridx;
    uint32_t ccrate;
    uint16_t new_ccseq;
    double new_rtt;

    cong_ctrl = (struct cong_ctrl_h *)message;
    cc_list = (struct cc_item *)(message + sizeof(struct cong_ctrl_h));
    listlen = meslen - (cong_ctrl->hlen * 4);

    if ((meslen < (cong_ctrl->hlen * 4U)) ||
            ((cong_ctrl->hlen * 4U) < sizeof(struct cong_ctrl_h))) {
        log2(group->group_id, group->file_id,
                "Rejecting CONG_CTRL: invalid message size");
        return;
    }
    if (group->cc_type != CC_TFMCC) {
        log3(group->group_id, group->file_id, "Rejecting CONG_CTRL: "
                "not allowed for given congestion control type");
        return;
    }

    new_ccseq = ntohs(cong_ctrl->cc_seq);
    ccrate = unquantize_rate(ntohs(cong_ctrl->cc_rate));
    group->last_server_ts.tv_sec = ntohl(cong_ctrl->tstamp_sec);
    group->last_server_ts.tv_usec = ntohl(cong_ctrl->tstamp_usec);
    group->last_server_rx_ts = rxtime;

    for (clridx = -1, ccidx = -1, i = 0;
            (i < listlen) && ((clridx == -1) || (ccidx == -1)); i++) {
        if (cc_list[i].dest_id == uid) {
            ccidx = i;
        }
        if ((cc_list[i].flags & FLAG_CC_CLR) != 0) {
            clridx = i;
        }
    }
    if (ccidx != -1) {
        new_rtt = unquantize_grtt(cc_list[ccidx].rtt);
        if (ccidx == clridx) {
            group->isclr = 1;
            send_cc_ack(group);
        } else {
            group->isclr = 0;
        }
        if (group->isclr) {
            group->rtt = (0.9 * group->rtt) + (0.1 * new_rtt);
        } else {
            group->rtt = (0.5 * group->rtt) + (0.5 * new_rtt);
        }
    } else {
        group->isclr = 0;
    }

    if (((int16_t)(new_ccseq - group->ccseq)) > 0) {
        init_tfmcc_fb_round(group, new_ccseq);
    } else if ((group->cc_time.tv_sec != 0) &&
            ((ccrate < current_cc_rate(group)) || (ccrate < group->initrate)) &&
            (group->grtt > group->rtt)) {
        log4(group->group_id, group->file_id, "Canceling feedback timer");
        group->cc_time.tv_sec = 0;
        group->cc_time.tv_usec = 0;
    }
}

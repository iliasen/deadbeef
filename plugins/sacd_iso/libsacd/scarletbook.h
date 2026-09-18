#ifndef SCARLETBOOK_H_INCLUDED
#define SCARLETBOOK_H_INCLUDED

#include <inttypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SACD_LSN_SIZE                  2048
#define SACD_SAMPLING_FREQUENCY        2822400
#define SACD_FRAME_RATE                75
#define START_OF_MASTER_TOC            510
#define MASTER_TOC_LEN                 10
#define MAX_AREA_TOC_SIZE_LSN          96
#define MAX_LANGUAGE_COUNT             8
#define MAX_CHANNEL_COUNT              6
#define MAX_DST_SIZE                   (1024 * 64)
#define SAMPLES_PER_FRAME              588
#define FRAME_SIZE_64                  4704
#define MAX_PACKET_SIZE                2045
#define SUPPORTED_VERSION_MAJOR        1
#define SUPPORTED_VERSION_MINOR        20
#define MAX_GENRE_COUNT                29
#define MAX_CATEGORY_COUNT             3
#define MAX_PROCESSING_BLOCK_SIZE      512

enum frame_format_t {
    FRAME_FORMAT_DST = 0,
    FRAME_FORMAT_DSD_3_IN_14 = 2,
    FRAME_FORMAT_DSD_3_IN_16 = 3
};

enum character_set_t {
    CHAR_SET_UNKNOWN       = 0,
    CHAR_SET_ISO646        = 1,
    CHAR_SET_ISO8859_1     = 2,
    CHAR_SET_RIS506        = 3,
    CHAR_SET_KSC5601       = 4,
    CHAR_SET_GB2312        = 5,
    CHAR_SET_BIG5          = 6,
    CHAR_SET_ISO8859_1_ESC = 7
};

extern const char *character_set[];
extern const char *album_genre[];
extern const char *album_category[];

#pragma pack(push, 1)

typedef struct {
    uint8_t  category;
    uint16_t reserved;
    uint8_t  genre;
} genre_table_t;

typedef struct {
    char    language_code[2];
    uint8_t character_set;
    uint8_t reserved;
} locale_table_t;

typedef struct {
    char           id[8];
    struct { uint8_t major; uint8_t minor; } version;
    uint8_t        reserved01[6];
    uint16_t       album_set_size;
    uint16_t       album_sequence_number;
    uint8_t        reserved02[4];
    char           album_catalog_number[16];
    genre_table_t  album_genre[4];
    uint8_t        reserved03[8];
    uint32_t       area_1_toc_1_start;
    uint32_t       area_1_toc_2_start;
    uint32_t       area_2_toc_1_start;
    uint32_t       area_2_toc_2_start;
    uint8_t        disc_type_hybrid : 1;
    uint8_t        disc_type_reserved : 7;
    uint8_t        reserved04[3];
    uint16_t       area_1_toc_size;
    uint16_t       area_2_toc_size;
    char           disc_catalog_number[16];
    genre_table_t  disc_genre[4];
    uint16_t       disc_date_year;
    uint8_t        disc_date_month;
    uint8_t        disc_date_day;
    uint8_t        reserved05[4];
    uint8_t        text_area_count;
    uint8_t        reserved06[7];
    locale_table_t locales[MAX_LANGUAGE_COUNT];
} master_toc_t;

typedef struct {
    char     id[8];
    uint8_t  reserved[8];
    uint16_t album_title_position;
    uint16_t album_artist_position;
    uint16_t album_publisher_position;
    uint16_t album_copyright_position;
    uint16_t album_title_phonetic_position;
    uint16_t album_artist_phonetic_position;
    uint16_t album_publisher_phonetic_position;
    uint16_t album_copyright_phonetic_position;
    uint16_t disc_title_position;
    uint16_t disc_artist_position;
    uint16_t disc_publisher_position;
    uint16_t disc_copyright_position;
    uint16_t disc_title_phonetic_position;
    uint16_t disc_artist_phonetic_position;
    uint16_t disc_publisher_phonetic_position;
    uint16_t disc_copyright_phonetic_position;
    uint8_t  data[2000];
} master_sacd_text_t;

typedef struct {
    char *album_title; char *album_title_phonetic;
    char *album_artist; char *album_artist_phonetic;
    char *album_publisher; char *album_publisher_phonetic;
    char *album_copyright; char *album_copyright_phonetic;
    char *disc_title; char *disc_title_phonetic;
    char *disc_artist; char *disc_artist_phonetic;
    char *disc_publisher; char *disc_publisher_phonetic;
    char *disc_copyright; char *disc_copyright_phonetic;
} master_text_t;

typedef struct {
    char    id[8];
    uint8_t information[2040];
} master_man_t;

typedef struct {
    char           id[8];
    struct { uint8_t major; uint8_t minor; } version;
    uint16_t       size;
    uint8_t        reserved01[4];
    uint32_t       max_byte_rate;
    uint8_t        sample_frequency;
    uint8_t        frame_format : 4;
    uint8_t        reserved02 : 4;
    uint8_t        reserved03[10];
    uint8_t        channel_count;
    uint8_t        loudspeaker_config : 5;
    uint8_t        extra_settings : 3;
    uint8_t        max_available_channels;
    uint8_t        area_mute_flags;
    uint8_t        reserved04[12];
    uint8_t        track_attribute : 4;
    uint8_t        reserved05 : 4;
    uint8_t        reserved06[15];
    struct { uint8_t minutes; uint8_t seconds; uint8_t frames; } total_playtime;
    uint8_t        reserved07;
    uint8_t        track_offset;
    uint8_t        track_count;
    uint8_t        reserved08[2];
    uint32_t       track_start;
    uint32_t       track_end;
    uint8_t        text_area_count;
    uint8_t        reserved09[7];
    locale_table_t languages[10];
    uint16_t       track_text_offset;
    uint16_t       index_list_offset;
    uint16_t       access_list_offset;
    uint8_t        reserved10[10];
    uint16_t       area_description_offset;
    uint16_t       copyright_offset;
    uint16_t       area_description_phonetic_offset;
    uint16_t       copyright_phonetic_offset;
    uint8_t        data[1896];
} area_toc_t;

typedef struct {
    char *track_type_title; char *track_type_performer;
    char *track_type_songwriter; char *track_type_composer;
    char *track_type_arranger; char *track_type_message;
    char *track_type_extra_message;
    char *track_type_title_phonetic; char *track_type_performer_phonetic;
    char *track_type_songwriter_phonetic; char *track_type_composer_phonetic;
    char *track_type_arranger_phonetic; char *track_type_message_phonetic;
    char *track_type_extra_message_phonetic;
} area_track_text_t;

typedef struct {
    char     id[8];
    uint16_t track_text_position[255];
} area_text_t;

typedef struct {
    char country_code[2];
    char owner_code[3];
    char recording_year[2];
    char designation_code[5];
} isrc_t;

typedef struct {
    char          id[8];
    isrc_t        isrc[255];
    uint32_t      reserved;
    genre_table_t track_genre[255];
} area_isrc_genre_t;

typedef struct {
    char        id[8];
    uint16_t    entry_count;
    uint8_t     main_step_size;
    uint8_t     reserved01[5];
    uint8_t     main_access_list[6550][5];
    uint8_t     reserved02[2];
    uint8_t     detailed_access_list[32768];
} area_access_list_t;

typedef struct {
    char     id[8];
    uint32_t track_start_lsn[255];
    uint32_t track_length_lsn[255];
} area_tracklist_offset_t;

typedef struct {
    uint8_t minutes; uint8_t seconds; uint8_t frames;
    uint8_t track_flags_tmf1 : 1; uint8_t track_flags_tmf2 : 1;
    uint8_t track_flags_tmf3 : 1; uint8_t track_flags_tmf4 : 1;
    uint8_t track_flags_ilp : 1; uint8_t reserved : 3;
} area_tracklist_time_t;

#define TIME_FRAMECOUNT(m) ((uint32_t)(m)->minutes * 60 * SACD_FRAME_RATE + (uint32_t)(m)->seconds * SACD_FRAME_RATE + (m)->frames)

typedef struct {
    char                id[8];
    area_tracklist_time_t start[255];
    area_tracklist_time_t duration[255];
} area_tracklist_t;

enum audio_packet_data_type_t {
    DATA_TYPE_AUDIO         = 2,
    DATA_TYPE_SUPPLEMENTARY = 3,
    DATA_TYPE_PADDING       = 7
};

typedef struct {
    uint8_t  frame_start   : 1;
    uint8_t  reserved      : 1;
    uint8_t  data_type     : 3;
    uint16_t packet_length : 11;
} audio_packet_info_t;
#define AUDIO_PACKET_INFO_SIZE    2U

typedef struct {
    struct { uint8_t minutes; uint8_t seconds; uint8_t frames; } timecode;
    uint8_t channel_bit_3 : 1; uint8_t channel_bit_2 : 1;
    uint8_t sector_count  : 5; uint8_t channel_bit_1 : 1;
} audio_frame_info_t;
#define AUDIO_FRAME_INFO_SIZE    4U

typedef struct {
    uint8_t dst_encoded       : 1;
    uint8_t reserved          : 1;
    uint8_t frame_info_count  : 3;
    uint8_t packet_info_count : 3;
} audio_frame_header_t;
#define AUDIO_SECTOR_HEADER_SIZE    1U

typedef struct {
    audio_frame_header_t    header;
    audio_packet_info_t     packet[7];
    audio_frame_info_t      frame[7];
} audio_sector_t;

typedef struct {
    uint8_t *area_data;
    area_toc_t *area_toc;
    area_tracklist_offset_t *area_tracklist_offset;
    area_tracklist_t *area_tracklist_time;
    area_text_t *area_text;
    area_track_text_t area_track_text[255];
    area_isrc_genre_t *area_isrc_genre;
    char *description; char *copyright;
    char *description_phonetic; char *copyright_phonetic;
} scarletbook_area_t;

typedef struct {
    uint8_t *data;
    int size; int started;
    int sector_count; int channel_count;
    int dst_encoded;
    struct { uint8_t minutes; uint8_t seconds; uint8_t frames; } timecode;
} scarletbook_audio_frame_t;

typedef struct sacd_reader_s sacd_reader_t;

typedef struct {
    sacd_reader_t *sacd;
    uint8_t *master_data;
    master_toc_t *master_toc;
    master_man_t *master_man;
    master_text_t master_text;
    int twoch_area_idx; int mulch_area_idx; int area_count;
    scarletbook_area_t area[4];
    scarletbook_audio_frame_t frame;
    audio_sector_t audio_sector;
    int frame_info_idx;
    int audio_frame_trimming;
    uint32_t count_frames;
    int dsf_nopad;
    int concatenate;
    int id3_tag_mode;
} scarletbook_handle_t;

#pragma pack(pop)

typedef void (*frame_read_callback_t)(scarletbook_handle_t *, uint8_t *data, int size, void *userdata);

scarletbook_handle_t *scarletbook_open(sacd_reader_t *sacd);
void scarletbook_close(scarletbook_handle_t *handle);
void scarletbook_frame_init(scarletbook_handle_t *handle);
int scarletbook_process_frames(scarletbook_handle_t *handle, uint8_t *read_buffer, int blocks_read_in, int last_block, frame_read_callback_t frame_read_callback, void *userdata);

int has_two_channel(scarletbook_handle_t *handle);
int has_multi_channel(scarletbook_handle_t *handle);
char *get_album_dir(scarletbook_handle_t *handle, int artist_flag);
char *get_music_filename(scarletbook_handle_t *handle, int area_idx, int track_idx, const char *conc_string, int performer_flag);
char *get_path_disc_album(scarletbook_handle_t *handle, int artist_flag);
char *get_speaker_config_string(area_toc_t *area_toc);

#ifdef __cplusplus
}
#endif

#endif
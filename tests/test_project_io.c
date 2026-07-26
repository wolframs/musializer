#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "project_io.h"
#include "sha256.h"
#include "test_support.h"
#include <float.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static void hash(char *out,char c){memset(out,c,64);out[64]=0;}
static Musi_Project fixture(void)
{
    Musi_Project p;musi_project_init(&p);
    strcpy(p.metadata.project_id,"project-1");strcpy(p.metadata.title,"Kitty \"Atlas\"\n世界");
    strcpy(p.metadata.author,"Wolfram");strcpy(p.metadata.created_utc,"2026-07-12T10:00:00Z");
    strcpy(p.metadata.modified_utc,"2026-07-12T11:00:00Z");strcpy(p.metadata.application_version,"0.2");
    p.audio.mode=MUSI_ASSET_REFERENCED;strcpy(p.audio.path,"audio\\kitty.mp3");hash(p.audio.sha256,'a');
    p.audio.duration_seconds=10.5;p.audio.sample_rate=48000;p.audio.channels=2;
    p.ascii_image.present=true;strcpy(p.ascii_image.path,"show.assets/images/face.png");hash(p.ascii_image.sha256,'c');p.ascii_image.columns=96;p.ascii_image.rows=54;
    // Every field non-default, so the round-trip memcmp below actually checks
    // the caption style rather than confirming that zeroed defaults survive.
    p.caption_style.face=MUSI_CAPTION_FACE_IMPORTED;p.caption_style.box=MUSI_CAPTION_BOX_SHADOW;
    p.caption_style.anchor=MUSI_CAPTION_ANCHOR_TOP_RIGHT;p.caption_style.size_scale=0.0725;
    p.caption_style.margin_scale=0.125;p.caption_style.width_scale=0.4;
    p.caption_style.text_rgba=0x1A2B3C4Du;p.caption_style.box_rgba=0xFFEEDDCCu;
    p.caption_style.font.present=true;strcpy(p.caption_style.font.path,"show.assets/fonts/inter.ttf");
    hash(p.caption_style.font.sha256,'d');strcpy(p.caption_style.font.family,"Inter");
    strcpy(p.caption_style.font.licence_path,"show.assets/fonts/inter.licence.txt");
    hash(p.caption_style.font.licence_sha256,'f');
    strcpy(p.caption_style.font.licence_name,"OFL-1.1");
    p.lyrics.duration_seconds=10.5;
    p.output.width=1920;p.output.height=1080;p.output.fps_numerator=30000;p.output.fps_denominator=1001;
    /* validator currently caps numerator at 1000 */ p.output.fps_numerator=60;
    p.output.start_seconds=.25;p.output.end_seconds=10.5;p.output.format=MUSI_OUTPUT_WEBM_VP9;
    p.output.quality=MUSI_OUTPUT_QUALITY_MASTER;
    p.deterministic_seed=UINT64_MAX;p.scene_count=1;
    Musi_Scene_Entry*s=&p.scenes[0];s->instance_id=UINT64_MAX-1;strcpy(s->scene_type,"atlas");s->enabled=true;
    s->start_seconds=0;s->end_seconds=10.5;s->opacity=.75;s->blend_mode=MUSI_BLEND_SCREEN;s->mapping_count=1;
    Musi_Parameter_Mapping*m=&s->mappings[0];strcpy(m->parameter,"glow");m->source=MUSI_ANALYSIS_BAND;m->band_index=65535;
    m->input_min=-1.25;m->input_max=2.5;m->output_min=-3;m->output_max=4.125;m->interpolation=MUSI_INTERPOLATION_EASE_OUT;m->clamp=false;
    p.cue_count=1;Musi_Parameter_Cue*c=&p.cues[0];c->cue_id=UINT64_MAX;c->target_scene_id=s->instance_id;strcpy(c->parameter,"glow");
    c->start_seconds=1;c->end_seconds=2;c->from_value=-.5;c->to_value=1.25;c->interpolation=MUSI_INTERPOLATION_STEP;
    p.analysis_lane_count=1;Musi_Analysis_Lane_Reference*l=&p.analysis_lanes[0];l->kind=MUSI_LANE_LYRIC_TIMING;
    strcpy(l->path,"analysis/lyrics.json");hash(l->sha256,'b');strcpy(l->audio_sha256,p.audio.sha256);
    strcpy(l->provenance.adapter,"whisper");strcpy(l->provenance.adapter_version,"1");strcpy(l->provenance.schema_version,"lyrics/v1");
    strcpy(l->provenance.model,"medium.en");strcpy(l->provenance.provider,"local");strcpy(l->provenance.prompt_version,"review-v1");
    p.lyrics.next_id=44;p.lyrics.count=2;p.lyrics.cues[0]=(Lyric_Cue){.id=42,.start_seconds=.5,.end_seconds=1.5};strcpy(p.lyrics.cues[0].text,"hello 世界");
    p.lyrics.cues[1]=(Lyric_Cue){.id=43,.start_seconds=2,.end_seconds=3};strcpy(p.lyrics.cues[1].text,"kitty");
    p.scene_switches.enabled=true;p.scene_switches.count=2;
    p.scene_switches.cues[0]=(Musi_Scene_Switch_Suggestion){.id=51,.start_seconds=0,.end_seconds=5,.strength=.25f};strcpy(p.scene_switches.cues[0].scene_name,"spectrum");
    p.scene_switches.cues[1]=(Musi_Scene_Switch_Suggestion){.id=52,.start_seconds=5,.end_seconds=10.5,.strength=.9f};strcpy(p.scene_switches.cues[1].scene_name,"atlas");
    p.scene_switches.cues[0].setting_count=3;p.scene_switches.cues[0].settings[0]=1.1f;p.scene_switches.cues[0].settings[1]=.8f;p.scene_switches.cues[0].settings[2]=1.0f;
    p.scene_switches.cues[1].setting_count=8;p.scene_switches.cues[1].settings[0]=1.2f;p.scene_switches.cues[1].settings[1]=2.4f;p.scene_switches.cues[1].settings[7]=1.0f;
    p.scene_preset_count=1;p.scene_presets[0]=(Musi_Scene_Preset){.id=81,.setting_count=8,.settings={1.2f,2.4f,1.0f,1.3f,1.0f,20.0f,.8f,1.0f}};strcpy(p.scene_presets[0].scene_name,"atlas");strcpy(p.scene_presets[0].name,"Wide wireframe");
    p.semantic_events.count=2;
    p.semantic_events.events[0]=(Event_Record){.timestamp_seconds=0,.id=71,.type=EVENT_TYPE_SEMANTIC,.value_count=4,.values={.25f,.5f,-.2f,.9f}};
    p.semantic_events.events[1]=(Event_Record){.timestamp_seconds=5,.id=72,.type=EVENT_TYPE_SEMANTIC,.value_count=4,.values={.75f,.8f,.4f,1}};
    p.manual_events.count=2;
    p.manual_events.events[0]=(Event_Record){.timestamp_seconds=1,.id=61,.type=EVENT_TYPE_CUE,.value_count=2,.values={.5f,1}};
    p.manual_events.events[1]=(Event_Record){.timestamp_seconds=2,.id=62,.type=EVENT_TYPE_CUSTOM,.value_count=1,.values={-.25f}};
    return p;
}
static char *encode(const Musi_Project*p,size_t*size)
{
    size_t needed=0;
    if(musi_project_json_serialize(p,NULL,0,&needed)!=MUSI_PROJECT_IO_ERROR_OUTPUT_TOO_SMALL){TEST_FAIL("project measure failed");return NULL;}
    char*out=malloc(needed);if(!out){TEST_FAIL("project JSON allocation failed");return NULL;}
    if(musi_project_json_serialize(p,out,needed,&needed)!=MUSI_PROJECT_IO_OK){TEST_FAIL("project serialization failed");free(out);return NULL;}
    *size=needed-1;return out;
}

TEST(project_io_round_trip_preserves_every_field_and_uint64)
{
    Musi_Project p=fixture(),decoded;size_t n;char*json=encode(&p,&n);if(!json)return;
    REQUIRE_TRUE(musi_project_json_deserialize(&decoded,json,n)==MUSI_PROJECT_IO_OK);
    EXPECT_TRUE(memcmp(&p,&decoded,sizeof(p))==0);EXPECT_TRUE(strstr(json,"18446744073709551615")!=NULL);
    EXPECT_TRUE(strstr(json,"\\\"Atlas\\\"\\n")!=NULL);
    EXPECT_TRUE(strstr(json,"\"semantic_events\":[{\"timestamp_seconds\":0")!=NULL);free(json);
}

TEST(project_io_early_v1_without_semantic_events_opens_with_empty_lane)
{
    Musi_Project p=fixture(),decoded;size_t n;char*json=encode(&p,&n);if(!json)return;
    char*begin=strstr(json,",\"semantic_events\":[");REQUIRE_TRUE(begin!=NULL);
    char*end=strstr(begin,"],\"manual_events\":");REQUIRE_TRUE(end!=NULL);++end;
    size_t prefix=(size_t)(begin-json),suffix=n-(size_t)(end-json);
    char*legacy=malloc(prefix+suffix+1);REQUIRE_TRUE(legacy!=NULL);
    memcpy(legacy,json,prefix);memcpy(legacy+prefix,end,suffix);legacy[prefix+suffix]=0;
    REQUIRE_TRUE(musi_project_json_deserialize(&decoded,legacy,prefix+suffix)==MUSI_PROJECT_IO_OK);
    EXPECT_EQ_SIZE(decoded.semantic_events.count,0);
    EXPECT_EQ_U64(decoded.semantic_events.revision,1);
    EXPECT_EQ_SIZE(decoded.manual_events.count,p.manual_events.count);
    free(legacy);free(json);
}

TEST(project_io_original_v1_defaults_new_workspace_fields)
{
    Musi_Project p=fixture(),decoded;size_t n;char*json=encode(&p,&n);if(!json)return;
    char*quality=strstr(json,",\"quality\":\"master\"");REQUIRE_TRUE(quality!=NULL);
    size_t quality_length=strlen(",\"quality\":\"master\"");
    memmove(quality,quality+quality_length,strlen(quality+quality_length)+1);
    char*workspace=strstr(json,",\"lyrics\":");REQUIRE_TRUE(workspace!=NULL);
    workspace[0]='}';workspace[1]='\0';n=strlen(json);

    REQUIRE_TRUE(musi_project_json_deserialize(&decoded,json,n)==MUSI_PROJECT_IO_OK);
    EXPECT_TRUE(decoded.output.quality==MUSI_OUTPUT_QUALITY_HIGH);
    EXPECT_EQ_SIZE(decoded.lyrics.count,0);
    EXPECT_EQ_U64(decoded.lyrics.next_id,1);
    EXPECT_NEAR(decoded.lyrics.duration_seconds,p.audio.duration_seconds,0.0);
    EXPECT_EQ_SIZE(decoded.scene_switches.count,0);
    EXPECT_FALSE(decoded.scene_switches.enabled);
    EXPECT_EQ_SIZE(decoded.semantic_events.count,0);
    EXPECT_EQ_SIZE(decoded.manual_events.count,0);
    free(json);
}

TEST(project_io_early_v1_defaults_optional_scene_authoring_fields)
{
    Musi_Project p=fixture(),decoded;size_t n;char*json=encode(&p,&n);if(!json)return;

    char *cursor=strstr(json,"\"scene_switches\":");REQUIRE_TRUE(cursor!=NULL);
    for(size_t i=0;i<p.scene_switches.count;++i){
        char *settings=strstr(cursor,",\"settings\":[");REQUIRE_TRUE(settings!=NULL);
        char *end=strchr(settings,']');REQUIRE_TRUE(end!=NULL);
        memmove(settings,end+1,strlen(end+1)+1);cursor=settings;
    }
    char *presets=strstr(json,",\"scene_presets\":[");REQUIRE_TRUE(presets!=NULL);
    char *events_after_presets=strstr(presets,"],\"semantic_events\":");
    REQUIRE_TRUE(events_after_presets!=NULL);
    memmove(presets,events_after_presets+1,strlen(events_after_presets+1)+1);
    char *ascii=strstr(json,",\"ascii_image\":");REQUIRE_TRUE(ascii!=NULL);
    char *output_after_ascii=strstr(ascii,",\"output\":");
    REQUIRE_TRUE(output_after_ascii!=NULL);
    memmove(ascii,output_after_ascii,strlen(output_after_ascii)+1);n=strlen(json);

    REQUIRE_TRUE(musi_project_json_deserialize(&decoded,json,n)==MUSI_PROJECT_IO_OK);
    EXPECT_FALSE(decoded.ascii_image.present);
    EXPECT_EQ_SIZE(decoded.scene_preset_count,0);
    EXPECT_EQ_SIZE(decoded.scene_switches.count,p.scene_switches.count);
    for(size_t i=0;i<decoded.scene_switches.count;++i){
        EXPECT_EQ_SIZE(decoded.scene_switches.cues[i].setting_count,0);
        EXPECT_TRUE(strcmp(decoded.scene_switches.cues[i].scene_name,
                           p.scene_switches.cues[i].scene_name)==0);
    }
    free(json);
}

TEST(project_io_embedded_semantics_survive_without_provenance_artifact)
{
    Musi_Project p=fixture(),decoded;
    p.analysis_lanes[0].kind=MUSI_LANE_SEMANTIC_SCORE;
    strcpy(p.analysis_lanes[0].path,"analysis/vanished.bridge.tsv");
    size_t n;char*json=encode(&p,&n);if(!json)return;
    REQUIRE_TRUE(musi_project_json_deserialize(&decoded,json,n)==MUSI_PROJECT_IO_OK);
    EXPECT_EQ_SIZE(decoded.semantic_events.count,2);
    EXPECT_EQ_U64(decoded.semantic_events.events[0].id,71);
    EXPECT_NEAR(decoded.semantic_events.events[1].values[0],.75f,0.0);
    EXPECT_TRUE(strcmp(decoded.analysis_lanes[0].path,"analysis/vanished.bridge.tsv")==0);
    free(json);
}

TEST(project_io_round_trips_all_enum_spellings)
{
    Musi_Project *p=malloc(sizeof(*p)),*d=malloc(sizeof(*d));REQUIRE_TRUE(p&&d);
#define ROUNDTRIP_ENUM(setter, actual, count) do { for(int value=0;value<(count);++value){*p=fixture();setter;size_t n;char*j=encode(p,&n);if(!j){free(p);free(d);return;}REQUIRE_TRUE(musi_project_json_deserialize(d,j,n)==MUSI_PROJECT_IO_OK);EXPECT_EQ_SIZE((actual),value);free(j);}}while(0)
    ROUNDTRIP_ENUM(p->output.format=value,d->output.format,MUSI_OUTPUT_FORMAT_COUNT);
    ROUNDTRIP_ENUM(p->output.quality=value,d->output.quality,MUSI_OUTPUT_QUALITY_COUNT);
    ROUNDTRIP_ENUM(p->scenes[0].mappings[0].source=value;if(value!=MUSI_ANALYSIS_BAND)p->scenes[0].mappings[0].band_index=0,d->scenes[0].mappings[0].source,MUSI_ANALYSIS_SOURCE_COUNT);
    ROUNDTRIP_ENUM(p->audio.mode=value,d->audio.mode,MUSI_ASSET_MODE_COUNT);
    ROUNDTRIP_ENUM(p->scenes[0].blend_mode=value,d->scenes[0].blend_mode,MUSI_BLEND_MODE_COUNT);
    ROUNDTRIP_ENUM(p->scenes[0].mappings[0].interpolation=value;p->cues[0].interpolation=value,d->cues[0].interpolation,MUSI_INTERPOLATION_COUNT);
    ROUNDTRIP_ENUM(p->analysis_lanes[0].kind=value,d->analysis_lanes[0].kind,MUSI_LANE_KIND_COUNT);
#undef ROUNDTRIP_ENUM
    free(p);free(d);
}

TEST(project_io_output_is_untouched_on_failure)
{
    Musi_Project p=fixture();char out[8]="keep";size_t needed=0;
    EXPECT_TRUE(musi_project_json_serialize(&p,out,sizeof(out),&needed)==MUSI_PROJECT_IO_ERROR_OUTPUT_TOO_SMALL);
    EXPECT_TRUE(strcmp(out,"keep")==0);p.scenes[0].opacity=NAN;
    EXPECT_TRUE(musi_project_json_serialize(&p,out,sizeof(out),&needed)==MUSI_PROJECT_IO_ERROR_VALIDATION);
    EXPECT_TRUE(strcmp(out,"keep")==0);
}

TEST(project_io_rejects_unknown_duplicate_missing_and_trailing_data_atomically)
{
    Musi_Project original=fixture(),out=original;size_t n;char*j=encode(&original,&n);if(!j)return;
    const char*unknown="{\"schema_version\":\"musializer.project/v1\",\"bogus\":1}";
    EXPECT_TRUE(musi_project_json_deserialize(&out,unknown,strlen(unknown))==MUSI_PROJECT_IO_ERROR_UNKNOWN_FIELD);
    EXPECT_TRUE(memcmp(&out,&original,sizeof(out))==0);
    const char*duplicate="{\"schema_version\":\"musializer.project/v1\",\"schema_version\":\"musializer.project/v1\"}";
    EXPECT_TRUE(musi_project_json_deserialize(&out,duplicate,strlen(duplicate))==MUSI_PROJECT_IO_ERROR_DUPLICATE_FIELD);
    const char*missing="{}";EXPECT_TRUE(musi_project_json_deserialize(&out,missing,2)==MUSI_PROJECT_IO_ERROR_MISSING_FIELD);
    char*trail=malloc(n+2);memcpy(trail,j,n);trail[n]='x';
    EXPECT_TRUE(musi_project_json_deserialize(&out,trail,n+1)==MUSI_PROJECT_IO_ERROR_SYNTAX);
    free(trail);free(j);
}

TEST(project_io_rejects_malformed_numbers_strings_and_oversize_input)
{
    Musi_Project out=fixture();
    const char*overflow="{\"schema_version\":\"musializer.project/v1\",\"deterministic_seed\":18446744073709551616}";
    EXPECT_TRUE(musi_project_json_deserialize(&out,overflow,strlen(overflow))==MUSI_PROJECT_IO_ERROR_NUMBER);
    const char*bad_escape="{\"schema_version\":\"musializer.project/v1\",\"x\":\"\\uD800\"}";
    EXPECT_TRUE(musi_project_json_deserialize(&out,bad_escape,strlen(bad_escape))!=MUSI_PROJECT_IO_OK);
    char one='x';EXPECT_TRUE(musi_project_json_deserialize(&out,&one,MUSI_PROJECT_JSON_MAX_INPUT+1)==MUSI_PROJECT_IO_ERROR_INPUT_SIZE);
}

TEST(project_io_decodes_unicode_surrogate_pairs)
{
    Musi_Project p=fixture();size_t n;char*j=encode(&p,&n);if(!j)return;
    char*title=strstr(j,"Kitty ");REQUIRE_TRUE(title!=NULL);(void)title;
    /* Full surrogate handling is exercised by replacing the existing UTF-8 title token. */
    const char*needle="Kitty \\\"Atlas\\\"\\n世界";char*at=strstr(j,needle);REQUIRE_TRUE(at!=NULL);
    const char*replacement="Kitty \\\"Atlas\\\"\\n\\uD83D\\uDE3A";size_t old=strlen(needle),nw=strlen(replacement);
    char*edited=malloc(n-old+nw+1);size_t prefix=(size_t)(at-j);memcpy(edited,j,prefix);memcpy(edited+prefix,replacement,nw);memcpy(edited+prefix+nw,at+old,n-prefix-old+1);
    Musi_Project d;REQUIRE_TRUE(musi_project_json_deserialize(&d,edited,n-old+nw)==MUSI_PROJECT_IO_OK);
    EXPECT_TRUE(strstr(d.metadata.title,"😺")!=NULL);free(edited);free(j);
}

TEST(project_io_round_trips_finite_double_extremes_and_non_c_locale)
{
    Musi_Project p=fixture(),d;p.scenes[0].mappings[0].output_min=-DBL_MAX;p.scenes[0].mappings[0].output_max=DBL_MAX;
    p.cues[0].from_value=DBL_MIN;p.cues[0].to_value=-DBL_MIN;
    char saved[128];const char*current=setlocale(LC_NUMERIC,NULL);snprintf(saved,sizeof(saved),"%s",current?current:"C");
    const char*changed=setlocale(LC_NUMERIC,"de_DE.UTF-8");if(!changed)changed=setlocale(LC_NUMERIC,"de_DE.utf8");
    size_t n;char*j=encode(&p,&n);if(!j){setlocale(LC_NUMERIC,saved);return;}
    EXPECT_TRUE(strstr(j,"0,25")==NULL);REQUIRE_TRUE(musi_project_json_deserialize(&d,j,n)==MUSI_PROJECT_IO_OK);
    EXPECT_TRUE(memcmp(&p.scenes[0].mappings[0].output_min,&d.scenes[0].mappings[0].output_min,sizeof(double))==0);
    EXPECT_TRUE(memcmp(&p.cues[0].from_value,&d.cues[0].from_value,sizeof(double))==0);
    free(j);setlocale(LC_NUMERIC,saved);
}

TEST(project_io_rejects_decoded_nul_oversize_strings_and_invalid_utf8_output)
{
    Musi_Project original=fixture(),out=original;size_t n;char*j=encode(&original,&n);if(!j)return;
    const char*needle="Kitty \\\"Atlas\\\"\\n世界";char*at=strstr(j,needle);REQUIRE_TRUE(at!=NULL);size_t old=strlen(needle);
    const char*nul="bad\\u0000title";size_t nl=strlen(nul);char*edited=malloc(n-old+nl+1);size_t prefix=(size_t)(at-j);
    memcpy(edited,j,prefix);memcpy(edited+prefix,nul,nl);memcpy(edited+prefix+nl,at+old,n-prefix-old+1);
    EXPECT_TRUE(musi_project_json_deserialize(&out,edited,n-old+nl)==MUSI_PROJECT_IO_ERROR_STRING);
    EXPECT_TRUE(memcmp(&out,&original,sizeof(out))==0);free(edited);
    size_t long_len=129;edited=malloc(n-old+long_len+1);memcpy(edited,j,prefix);memset(edited+prefix,'x',long_len);memcpy(edited+prefix+long_len,at+old,n-prefix-old+1);
    EXPECT_TRUE(musi_project_json_deserialize(&out,edited,n-old+long_len)==MUSI_PROJECT_IO_ERROR_STRING);free(edited);free(j);
    original.metadata.title[0]=(char)0xff;original.metadata.title[1]=0;char buffer[32]="untouched";size_t required=0;
    EXPECT_TRUE(musi_project_json_serialize(&original,buffer,sizeof(buffer),&required)==MUSI_PROJECT_IO_ERROR_STRING);
    EXPECT_TRUE(strcmp(buffer,"untouched")==0);
}

TEST(project_io_rejects_arrays_beyond_schema_capacity)
{
    Musi_Project p=fixture(),out=p;size_t n;char*j=encode(&p,&n);if(!j)return;
    char*begin=strstr(j,"\"scenes\":[");REQUIRE_TRUE(begin!=NULL);begin+=strlen("\"scenes\":[");
    char*end=strstr(begin,"],\"cues\"");REQUIRE_TRUE(end!=NULL);size_t item=(size_t)(end-begin),prefix=(size_t)(begin-j),suffix=n-(size_t)(end-j);
    size_t total=prefix+(item+1)*(MUSI_PROJECT_MAX_SCENES+1)+suffix;char*many=malloc(total);REQUIRE_TRUE(many!=NULL);size_t at=0;
    memcpy(many,j,prefix);at=prefix;for(size_t i=0;i<MUSI_PROJECT_MAX_SCENES+1;++i){if(i)many[at++]=',';memcpy(many+at,begin,item);at+=item;}memcpy(many+at,end,suffix);at+=suffix;
    EXPECT_TRUE(musi_project_json_deserialize(&out,many,at)==MUSI_PROJECT_IO_ERROR_CAPACITY);
    EXPECT_TRUE(memcmp(&out,&p,sizeof(out))==0);free(many);free(j);
}

TEST(project_io_authored_workspace_failures_are_atomic)
{
    Musi_Project expected=fixture(),out=expected;size_t n;char*j=encode(&expected,&n);if(!j)return;
    char*bad=malloc(n+1);REQUIRE_TRUE(bad!=NULL);
    memcpy(bad,j,n+1);char*at=strstr(bad,"\"scene_name\":\"spectrum\"");REQUIRE_TRUE(at!=NULL);
    at+=strlen("\"scene_name\":\"");*at='!';
    EXPECT_TRUE(musi_project_json_deserialize(&out,bad,n)==MUSI_PROJECT_IO_ERROR_VALIDATION);
    EXPECT_TRUE(memcmp(&out,&expected,sizeof(out))==0);

    memcpy(bad,j,n+1);at=strstr(bad,"\"id\":42,\"start_seconds\":0.5");REQUIRE_TRUE(at!=NULL);
    at=strstr(at,"0.5");*at='9';
    EXPECT_TRUE(musi_project_json_deserialize(&out,bad,n)==MUSI_PROJECT_IO_ERROR_VALIDATION);
    EXPECT_TRUE(memcmp(&out,&expected,sizeof(out))==0);

    memcpy(bad,j,n+1);at=strstr(bad,"hello 世界");REQUIRE_TRUE(at!=NULL);at[6]=(char)0xff;
    EXPECT_TRUE(musi_project_json_deserialize(&out,bad,n)==MUSI_PROJECT_IO_ERROR_STRING);
    EXPECT_TRUE(memcmp(&out,&expected,sizeof(out))==0);

    memcpy(bad,j,n+1);at=strstr(bad,"\"semantic_events\":[");REQUIRE_TRUE(at!=NULL);
    at=strstr(at,"\"type\":\"semantic\"");REQUIRE_TRUE(at!=NULL);
    at+=strlen("\"type\":\"");size_t semantic_offset=(size_t)(at-bad);
    memcpy(at,"custom",6);memmove(at+6,at+8,n-semantic_offset-8+1);
    EXPECT_TRUE(musi_project_json_deserialize(&out,bad,n-2)==MUSI_PROJECT_IO_ERROR_VALIDATION);
    EXPECT_TRUE(memcmp(&out,&expected,sizeof(out))==0);
    free(bad);free(j);
}

#ifndef _WIN32
static bool project_io_test_mkdir(char *output, size_t capacity)
{
    static unsigned sequence;
    for (unsigned attempt = 0; attempt < 100u; ++attempt) {
        int length = snprintf(output, capacity,
                              "/tmp/musializer-project-io-%ld-%u",
                              (long)getpid(), ++sequence);
        if (length <= 0 || (size_t)length >= capacity) return false;
        if (mkdir(output, 0700) == 0) return true;
        if (errno != EEXIST) return false;
    }
    return false;
}

static bool project_io_test_write(const char *path, const char *contents)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    size_t size = strlen(contents);
    bool ok = fwrite(contents, 1, size, file) == size;
    if (fclose(file) != 0) ok = false;
    return ok;
}

static bool project_io_test_read_equals(const char *path, const char *expected)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    char buffer[128];
    size_t count = fread(buffer, 1, sizeof(buffer) - 1u, file);
    bool complete = !ferror(file) && feof(file);
    (void)fclose(file);
    buffer[count] = '\0';
    return complete && strcmp(buffer, expected) == 0;
}

static size_t project_io_test_transaction_count(const char *directory)
{
    static const char prefix[] = ".musializer-project-";
    DIR *stream = opendir(directory);
    if (stream == NULL) return SIZE_MAX;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(stream)) != NULL) {
        if (strncmp(entry->d_name, prefix, sizeof(prefix) - 1u) == 0) ++count;
    }
    (void)closedir(stream);
    return count;
}

TEST(project_io_resolves_assets_beside_project_before_legacy_cwd_shadow)
{
    char original_cwd[4096];
    char root[256];
    char project_directory[320];
    char project_assets[352];
    char cwd_assets[352];
    char project_path[384];
    char beside_project[384];
    char cwd_shadow[384];
    bool changed_directory = false;

    if (getcwd(original_cwd, sizeof(original_cwd)) == NULL ||
        !project_io_test_mkdir(root, sizeof(root))) {
        TEST_FAIL("could not create project-path test sandbox");
        return;
    }
    snprintf(project_directory, sizeof(project_directory), "%s/project", root);
    snprintf(project_assets, sizeof(project_assets), "%s/assets", project_directory);
    snprintf(cwd_assets, sizeof(cwd_assets), "%s/assets", root);
    snprintf(project_path, sizeof(project_path), "%s/song.musi", project_directory);
    snprintf(beside_project, sizeof(beside_project), "%s/song.mp3", project_assets);
    snprintf(cwd_shadow, sizeof(cwd_shadow), "%s/song.mp3", cwd_assets);
    if (mkdir(project_directory, 0700) != 0 || mkdir(project_assets, 0700) != 0 ||
        mkdir(cwd_assets, 0700) != 0 ||
        !project_io_test_write(beside_project, "project copy") ||
        !project_io_test_write(cwd_shadow, "cwd shadow") || chdir(root) != 0) {
        TEST_FAIL("could not prepare project-path test sandbox");
        goto cleanup;
    }
    changed_directory = true;

    char resolved[512] = "untouched";
    Musi_Project_Path_Result result = musi_project_resolve_asset_path(
        project_path, "assets/song.mp3", resolved, sizeof(resolved));
    EXPECT_TRUE(result == MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE);
    EXPECT_TRUE(strcmp(resolved, beside_project) == 0);
    result = musi_project_resolve_asset_path(
        project_path, "assets\\song.mp3", resolved, sizeof(resolved));
    EXPECT_TRUE(result == MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE);
    EXPECT_TRUE(strcmp(resolved, beside_project) == 0);

    result = musi_project_canonicalize_existing_file(
        "assets/song.mp3", resolved, sizeof(resolved));
    EXPECT_TRUE(result == MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE);
    EXPECT_TRUE(strcmp(resolved, cwd_shadow) == 0);

    if (unlink(beside_project) != 0) {
        TEST_FAIL("could not remove project-relative asset during fallback test");
        goto cleanup;
    }
    strcpy(resolved, "untouched");
    result = musi_project_resolve_asset_path(
        project_path, "assets/song.mp3", resolved, sizeof(resolved));
    EXPECT_TRUE(result == MUSI_PROJECT_PATH_RESOLVED_LEGACY_CWD);
    EXPECT_TRUE(strcmp(resolved, "assets/song.mp3") == 0);

    if (unlink(cwd_shadow) != 0) {
        TEST_FAIL("could not remove working-directory asset during fallback test");
        goto cleanup;
    }
    strcpy(resolved, "untouched");
    result = musi_project_resolve_asset_path(
        project_path, "assets/song.mp3", resolved, sizeof(resolved));
    EXPECT_TRUE(result == MUSI_PROJECT_PATH_ERROR_NOT_FOUND);
    EXPECT_TRUE(strcmp(resolved, "untouched") == 0);

cleanup:
    if (changed_directory && chdir(original_cwd) != 0) {
        TEST_FAIL("could not restore working directory after project-path test");
    }
    (void)unlink(beside_project);
    (void)unlink(cwd_shadow);
    (void)rmdir(project_assets);
    (void)rmdir(cwd_assets);
    (void)rmdir(project_directory);
    (void)rmdir(root);
}

TEST(project_io_stores_safe_descendants_relative_with_absolute_fallback)
{
    char root[256];
    REQUIRE_TRUE(project_io_test_mkdir(root, sizeof(root)));
    char project_directory[320];
    char asset_directory[352];
    char deep_directory[384];
    char outside_directory[320];
    char project_path[384];
    char same_directory_asset[384];
    char deep_asset[416];
    char outside_asset[384];
    char traversal_asset[448];
    char ambiguous_asset[416];
    snprintf(project_directory, sizeof(project_directory), "%s/project", root);
    snprintf(asset_directory, sizeof(asset_directory), "%s/assets", project_directory);
    snprintf(deep_directory, sizeof(deep_directory), "%s/deep", asset_directory);
    snprintf(outside_directory, sizeof(outside_directory), "%s/outside", root);
    snprintf(project_path, sizeof(project_path), "%s/session.musi", project_directory);
    snprintf(same_directory_asset, sizeof(same_directory_asset),
             "%s/song.wav", project_directory);
    snprintf(deep_asset, sizeof(deep_asset), "%s/song.wav", deep_directory);
    snprintf(outside_asset, sizeof(outside_asset), "%s/song.wav", outside_directory);
    snprintf(traversal_asset, sizeof(traversal_asset),
             "%s/assets/../../outside/song.wav", project_directory);
    snprintf(ambiguous_asset, sizeof(ambiguous_asset),
             "%s/literal\\name.wav", project_directory);

    REQUIRE_TRUE(mkdir(project_directory, 0700) == 0);
    REQUIRE_TRUE(mkdir(asset_directory, 0700) == 0);
    REQUIRE_TRUE(mkdir(deep_directory, 0700) == 0);
    REQUIRE_TRUE(mkdir(outside_directory, 0700) == 0);
    REQUIRE_TRUE(project_io_test_write(same_directory_asset, "same"));
    REQUIRE_TRUE(project_io_test_write(deep_asset, "deep"));
    REQUIRE_TRUE(project_io_test_write(outside_asset, "outside"));
    REQUIRE_TRUE(project_io_test_write(ambiguous_asset, "ambiguous"));

    char canonical[512];
    char stored[512] = "untouched";
    char resolved[512];
    REQUIRE_TRUE(musi_project_canonicalize_existing_file(
        deep_asset, canonical, sizeof(canonical)) ==
        MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE);
    EXPECT_TRUE(musi_project_asset_path_for_storage(
        project_path, canonical, stored, sizeof(stored)) ==
        MUSI_PROJECT_STORED_PATH_RELATIVE);
    EXPECT_TRUE(strcmp(stored, "assets/deep/song.wav") == 0);
    EXPECT_TRUE(musi_project_resolve_asset_path(
        project_path, stored, resolved, sizeof(resolved)) ==
        MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE);
    EXPECT_TRUE(musi_project_existing_files_alias(resolved, canonical));

    REQUIRE_TRUE(musi_project_canonicalize_existing_file(
        same_directory_asset, canonical, sizeof(canonical)) ==
        MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE);
    EXPECT_TRUE(musi_project_asset_path_for_storage(
        project_path, canonical, stored, sizeof(stored)) ==
        MUSI_PROJECT_STORED_PATH_RELATIVE);
    EXPECT_TRUE(strcmp(stored, "song.wav") == 0);

    REQUIRE_TRUE(musi_project_canonicalize_existing_file(
        traversal_asset, canonical, sizeof(canonical)) ==
        MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE);
    EXPECT_TRUE(musi_project_asset_path_for_storage(
        project_path, canonical, stored, sizeof(stored)) ==
        MUSI_PROJECT_STORED_PATH_ABSOLUTE);
    EXPECT_TRUE(strcmp(stored, canonical) == 0);
    EXPECT_TRUE(musi_project_resolve_asset_path(
        project_path, stored, resolved, sizeof(resolved)) ==
        MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE);
    EXPECT_TRUE(musi_project_existing_files_alias(resolved, outside_asset));

    // On POSIX a backslash can be a literal filename character, while the
    // project resolver intentionally treats it as a portable separator. Such
    // a path must therefore remain absolute to preserve its identity.
    REQUIRE_TRUE(musi_project_canonicalize_existing_file(
        ambiguous_asset, canonical, sizeof(canonical)) ==
        MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE);
    EXPECT_TRUE(musi_project_asset_path_for_storage(
        project_path, canonical, stored, sizeof(stored)) ==
        MUSI_PROJECT_STORED_PATH_ABSOLUTE);
    EXPECT_TRUE(strcmp(stored, canonical) == 0);

    strcpy(stored, "untouched");
    EXPECT_TRUE(musi_project_asset_path_for_storage(
        project_path, canonical, stored, 2) ==
        MUSI_PROJECT_STORED_PATH_ERROR_TOO_LONG);
    EXPECT_TRUE(strcmp(stored, "untouched") == 0);
    EXPECT_TRUE(musi_project_asset_path_for_storage(
        project_path, "relative/song.wav", stored, sizeof(stored)) ==
        MUSI_PROJECT_STORED_PATH_ERROR_INVALID);
    EXPECT_TRUE(strcmp(stored, "untouched") == 0);
    EXPECT_TRUE(musi_project_asset_path_for_storage(
        project_path, "/definitely/not/a/musializer-asset.wav",
        stored, sizeof(stored)) == MUSI_PROJECT_STORED_PATH_ERROR_INVALID);
    EXPECT_TRUE(strcmp(stored, "untouched") == 0);

    char linked_directory[320];
    char linked_project[384];
    snprintf(linked_directory, sizeof(linked_directory), "%s/project-link", root);
    snprintf(linked_project, sizeof(linked_project), "%s/session.musi",
             linked_directory);
    REQUIRE_TRUE(symlink(project_directory, linked_directory) == 0);
    REQUIRE_TRUE(musi_project_canonicalize_existing_file(
        deep_asset, canonical, sizeof(canonical)) ==
        MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE);
    EXPECT_TRUE(musi_project_asset_path_for_storage(
        linked_project, canonical, stored, sizeof(stored)) ==
        MUSI_PROJECT_STORED_PATH_RELATIVE);
    EXPECT_TRUE(strcmp(stored, "assets/deep/song.wav") == 0);
    EXPECT_TRUE(musi_project_resolve_asset_path(
        linked_project, stored, resolved, sizeof(resolved)) ==
        MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE);
    EXPECT_TRUE(musi_project_existing_files_alias(resolved, canonical));

    (void)unlink(linked_directory);
    (void)unlink(ambiguous_asset);
    (void)unlink(outside_asset);
    (void)unlink(deep_asset);
    (void)unlink(same_directory_asset);
    (void)rmdir(deep_directory);
    (void)rmdir(asset_directory);
    (void)rmdir(outside_directory);
    (void)rmdir(project_directory);
    (void)rmdir(root);
}

TEST(project_io_bundles_content_addressed_assets_and_rejects_escape_or_collision)
{
    char root[256];
    REQUIRE_TRUE(project_io_test_mkdir(root, sizeof(root)));
    char project_directory[320];
    char project_path[384];
    char source[320];
    snprintf(project_directory, sizeof(project_directory), "%s/project", root);
    snprintf(project_path, sizeof(project_path), "%s/show.musi", project_directory);
    snprintf(source, sizeof(source), "%s/source.MP3", root);
    REQUIRE_TRUE(mkdir(project_directory, 0700) == 0);
    REQUIRE_TRUE(project_io_test_write(source, "immutable audio bytes"));
    char identity[SHA256_HEX_SIZE];
    REQUIRE_TRUE(sha256_file_hex(source, identity));

    char stored[512];
    char runtime[512];
    EXPECT_TRUE(musi_project_bundle_asset(
        project_path, MUSI_PROJECT_ASSET_AUDIO, source, identity,
        stored, sizeof(stored), runtime, sizeof(runtime)) ==
        MUSI_PROJECT_BUNDLE_OK);
    char expected[512];
    snprintf(expected, sizeof(expected), "show.assets/audio/%s.mp3", identity);
    EXPECT_TRUE(strcmp(stored, expected) == 0);
    EXPECT_TRUE(project_io_test_read_equals(runtime, "immutable audio bytes"));

    char resolved[512];
    EXPECT_TRUE(musi_project_resolve_bundled_asset_path(
        project_path, stored, resolved, sizeof(resolved)) ==
        MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE);
    EXPECT_TRUE(musi_project_existing_files_alias(resolved, runtime));
    EXPECT_TRUE(musi_project_bundle_asset(
        project_path, MUSI_PROJECT_ASSET_AUDIO, source, identity,
        stored, sizeof(stored), runtime, sizeof(runtime)) ==
        MUSI_PROJECT_BUNDLE_OK);
    EXPECT_TRUE(musi_project_reference_published_asset(
        project_path, MUSI_PROJECT_ASSET_AUDIO, runtime, identity,
        stored, sizeof(stored), resolved, sizeof(resolved)) ==
        MUSI_PROJECT_BUNDLE_OK);
    EXPECT_TRUE(musi_project_existing_files_alias(runtime, resolved));

    char other_project[384];
    snprintf(other_project, sizeof(other_project), "%s/other.musi",
             project_directory);
    EXPECT_TRUE(musi_project_reference_published_asset(
        other_project, MUSI_PROJECT_ASSET_AUDIO, runtime, identity,
        stored, sizeof(stored), resolved, sizeof(resolved)) ==
        MUSI_PROJECT_BUNDLE_ERROR_SOURCE);

    char escape[512];
    snprintf(escape, sizeof(escape), "%s/show.assets/audio/escape.mp3",
             project_directory);
    REQUIRE_TRUE(symlink(source, escape) == 0);
    EXPECT_TRUE(musi_project_resolve_bundled_asset_path(
        project_path, "show.assets/audio/escape.mp3", resolved,
        sizeof(resolved)) == MUSI_PROJECT_PATH_ERROR_NOT_FOUND);
    EXPECT_TRUE(musi_project_resolve_bundled_asset_path(
        project_path, "../source.MP3", resolved, sizeof(resolved)) ==
        MUSI_PROJECT_PATH_ERROR_NOT_FOUND);

    REQUIRE_TRUE(unlink(runtime) == 0);
    REQUIRE_TRUE(project_io_test_write(runtime, "different bytes"));
    EXPECT_TRUE(musi_project_bundle_asset(
        project_path, MUSI_PROJECT_ASSET_AUDIO, source, identity,
        stored, sizeof(stored), runtime, sizeof(runtime)) ==
        MUSI_PROJECT_BUNDLE_ERROR_COLLISION);

    char category_directory[448];
    char bundle_directory[416];
    snprintf(category_directory, sizeof(category_directory),
             "%s/show.assets/audio", project_directory);
    snprintf(bundle_directory, sizeof(bundle_directory),
             "%s/show.assets", project_directory);
    (void)unlink(escape);
    (void)unlink(runtime);
    REQUIRE_TRUE(rmdir(category_directory) == 0);
    REQUIRE_TRUE(rmdir(bundle_directory) == 0);

    char outside_bundle[416];
    snprintf(outside_bundle, sizeof(outside_bundle), "%s/outside-bundle", root);
    REQUIRE_TRUE(mkdir(outside_bundle, 0700) == 0);
    REQUIRE_TRUE(symlink(outside_bundle, bundle_directory) == 0);
    EXPECT_TRUE(musi_project_bundle_asset(
        project_path, MUSI_PROJECT_ASSET_AUDIO, source, identity,
        stored, sizeof(stored), runtime, sizeof(runtime)) ==
        MUSI_PROJECT_BUNDLE_ERROR_DIRECTORY);
    REQUIRE_TRUE(unlink(bundle_directory) == 0);
    REQUIRE_TRUE(rmdir(outside_bundle) == 0);

    (void)unlink(source);
    (void)rmdir(project_directory);
    (void)rmdir(root);
}

TEST(project_io_bundles_a_font_and_its_licence_under_the_fonts_category)
{
    EXPECT_TRUE(strcmp(musi_project_asset_category_directory(
        MUSI_PROJECT_ASSET_AUDIO), "audio") == 0);
    EXPECT_TRUE(strcmp(musi_project_asset_category_directory(
        MUSI_PROJECT_ASSET_IMAGE), "images") == 0);
    EXPECT_TRUE(strcmp(musi_project_asset_category_directory(
        MUSI_PROJECT_ASSET_FONT), "fonts") == 0);
    // A category the bundle machinery does not know must be refused, not
    // published into a directory named by whatever the switch fell through to.
    EXPECT_TRUE(musi_project_asset_category_directory(
        (Musi_Project_Asset_Category)(MUSI_PROJECT_ASSET_FONT + 1)) == NULL);
    EXPECT_TRUE(!musi_project_asset_category_valid(
        (Musi_Project_Asset_Category)(MUSI_PROJECT_ASSET_FONT + 1)));

    char root[256];
    REQUIRE_TRUE(project_io_test_mkdir(root, sizeof(root)));
    char project_directory[320];
    char project_path[384];
    char face[320];
    char licence[320];
    snprintf(project_directory, sizeof(project_directory), "%s/project", root);
    snprintf(project_path, sizeof(project_path), "%s/show.musi", project_directory);
    snprintf(face, sizeof(face), "%s/Inter-Regular.TTF", root);
    snprintf(licence, sizeof(licence), "%s/OFL.txt", root);
    REQUIRE_TRUE(mkdir(project_directory, 0700) == 0);
    REQUIRE_TRUE(project_io_test_write(face, "\x00\x01\x00\x00 pretend sfnt"));
    REQUIRE_TRUE(project_io_test_write(licence, "Copyright ... SIL Open Font License"));
    char face_identity[SHA256_HEX_SIZE];
    char licence_identity[SHA256_HEX_SIZE];
    REQUIRE_TRUE(sha256_file_hex(face, face_identity));
    REQUIRE_TRUE(sha256_file_hex(licence, licence_identity));

    char stored[512];
    char runtime[512];
    char expected[512];
    EXPECT_TRUE(musi_project_bundle_asset(
        project_path, MUSI_PROJECT_ASSET_FONT, face, face_identity,
        stored, sizeof(stored), runtime, sizeof(runtime)) ==
        MUSI_PROJECT_BUNDLE_OK);
    snprintf(expected, sizeof(expected), "show.assets/fonts/%s.ttf", face_identity);
    EXPECT_TRUE(strcmp(stored, expected) == 0);

    // The licence travels beside the face under the same category, so a
    // recipient who receives the bundle receives the terms with it.
    char licence_stored[512];
    char licence_runtime[512];
    EXPECT_TRUE(musi_project_bundle_asset(
        project_path, MUSI_PROJECT_ASSET_FONT, licence, licence_identity,
        licence_stored, sizeof(licence_stored),
        licence_runtime, sizeof(licence_runtime)) == MUSI_PROJECT_BUNDLE_OK);
    snprintf(expected, sizeof(expected), "show.assets/fonts/%s.txt",
             licence_identity);
    EXPECT_TRUE(strcmp(licence_stored, expected) == 0);

    char resolved[512];
    EXPECT_TRUE(musi_project_resolve_bundled_asset_path(
        project_path, stored, resolved, sizeof(resolved)) ==
        MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE);
    EXPECT_TRUE(musi_project_existing_files_alias(resolved, runtime));

    // Two different faces cannot collide, and the same face re-imported is a
    // no-op rather than a rewrite.
    EXPECT_TRUE(musi_project_bundle_asset(
        project_path, MUSI_PROJECT_ASSET_FONT, face, licence_identity,
        stored, sizeof(stored), runtime, sizeof(runtime)) ==
        MUSI_PROJECT_BUNDLE_ERROR_SOURCE);

    char category_directory[448];
    char bundle_directory[416];
    snprintf(category_directory, sizeof(category_directory),
             "%s/show.assets/fonts", project_directory);
    snprintf(bundle_directory, sizeof(bundle_directory),
             "%s/show.assets", project_directory);
    (void)unlink(runtime);
    (void)unlink(licence_runtime);
    (void)rmdir(category_directory);
    (void)rmdir(bundle_directory);
    (void)unlink(face);
    (void)unlink(licence);
    (void)rmdir(project_directory);
    (void)rmdir(root);
}

TEST(project_io_transaction_paths_are_distinct_and_atomic_writes_are_owned)
{
    EXPECT_TRUE(musi_project_process_id() > 0);
    char root[256];
    REQUIRE_TRUE(project_io_test_mkdir(root, sizeof(root)));
    char destination[320];
    char first[416];
    char second[416];
    snprintf(destination, sizeof(destination), "%s/session.musi", root);
    REQUIRE_TRUE(musi_project_temporary_path(destination, 7, 11,
                                             first, sizeof(first)));
    REQUIRE_TRUE(musi_project_temporary_path(destination, 7, 12,
                                             second, sizeof(second)));
    EXPECT_TRUE(strcmp(first, second) != 0);
    EXPECT_TRUE(strncmp(first, root, strlen(root)) == 0);
    EXPECT_TRUE(strncmp(second, root, strlen(root)) == 0);

    // Claim the first process transaction name. The writer must skip it and
    // must never remove a sibling it did not create.
    char collision[416];
    REQUIRE_TRUE(musi_project_temporary_path(destination, (uint64_t)getpid(), 1,
                                             collision, sizeof(collision)));
    REQUIRE_TRUE(project_io_test_write(collision, "foreign transaction"));
    REQUIRE_TRUE(project_io_test_write(destination, "old project"));
    REQUIRE_TRUE(chmod(destination, 0666) == 0);

    const char *replacement = "new durable project";
    mode_t previous_umask = umask(0002);
    EXPECT_TRUE(musi_project_atomic_write(destination, replacement,
                                          strlen(replacement)) ==
                MUSI_PROJECT_FILE_OK);
    (void)umask(previous_umask);
    EXPECT_TRUE(project_io_test_read_equals(destination, replacement));
    EXPECT_TRUE(project_io_test_read_equals(collision, "foreign transaction"));
    struct stat status;
    REQUIRE_TRUE(stat(destination, &status) == 0);
    EXPECT_TRUE((status.st_mode & 0777) == 0666);
    EXPECT_EQ_SIZE(project_io_test_transaction_count(root), 1);

    char alias[320];
    snprintf(alias, sizeof(alias), "%s/session-alias.musi", root);
    REQUIRE_TRUE(link(destination, alias) == 0);
    EXPECT_TRUE(musi_project_existing_files_alias(destination, alias));
    EXPECT_FALSE(musi_project_existing_files_alias(destination, collision));
    REQUIRE_TRUE(unlink(alias) == 0);

    REQUIRE_TRUE(unlink(collision) == 0);
    const char *newer = "second save";
    EXPECT_TRUE(musi_project_atomic_write(destination, newer, strlen(newer)) ==
                MUSI_PROJECT_FILE_OK);
    EXPECT_TRUE(project_io_test_read_equals(destination, newer));
    EXPECT_EQ_SIZE(project_io_test_transaction_count(root), 0);

    char directory_destination[320];
    snprintf(directory_destination, sizeof(directory_destination),
             "%s/not-a-file", root);
    REQUIRE_TRUE(mkdir(directory_destination, 0700) == 0);
    EXPECT_TRUE(musi_project_atomic_write(directory_destination, "x", 1) ==
                MUSI_PROJECT_FILE_ERROR_PUBLISH);
    EXPECT_EQ_SIZE(project_io_test_transaction_count(root), 0);

    (void)unlink(destination);
    (void)rmdir(directory_destination);
    (void)rmdir(root);
}
#endif

TEST(project_io_v1_without_caption_style_gets_the_shipped_defaults)
{
    // The point of making the member optional: a project written before
    // caption typography existed must still open, and must look exactly as it
    // did, which means the defaults have to be the old hard-coded values.
    Musi_Project p=fixture(),decoded;size_t n;char*json=encode(&p,&n);if(!json)return;
    char*begin=strstr(json,",\"caption_style\":");REQUIRE_TRUE(begin!=NULL);
    char*end=strstr(begin,",\"output\":");REQUIRE_TRUE(end!=NULL);
    size_t prefix=(size_t)(begin-json),suffix=n-(size_t)(end-json);
    char*legacy=malloc(prefix+suffix+1);REQUIRE_TRUE(legacy!=NULL);
    memcpy(legacy,json,prefix);memcpy(legacy+prefix,end,suffix);legacy[prefix+suffix]=0;
    REQUIRE_TRUE(musi_project_json_deserialize(&decoded,legacy,prefix+suffix)==MUSI_PROJECT_IO_OK);
    EXPECT_TRUE(musi_caption_style_is_default(&decoded.caption_style));
    EXPECT_TRUE(decoded.caption_style.face==MUSI_CAPTION_FACE_ALEGREYA);
    EXPECT_TRUE(decoded.caption_style.box==MUSI_CAPTION_BOX_PLATE);
    EXPECT_TRUE(decoded.caption_style.anchor==MUSI_CAPTION_ANCHOR_BOTTOM_CENTER);
    EXPECT_NEAR(decoded.caption_style.size_scale,0.047,0.0);
    free(legacy);free(json);
}

TEST(project_io_writes_colours_as_eight_lowercase_hex_digits)
{
    Musi_Project p=fixture();size_t n;char*json=encode(&p,&n);if(!json)return;
    EXPECT_TRUE(strstr(json,"\"text_rgba\":\"1a2b3c4d\"")!=NULL);
    EXPECT_TRUE(strstr(json,"\"box_rgba\":\"ffeeddcc\"")!=NULL);
    EXPECT_TRUE(strstr(json,"\"face\":\"imported\"")!=NULL);
    EXPECT_TRUE(strstr(json,"\"anchor\":\"top_right\"")!=NULL);
    free(json);
}

TEST(project_io_rejects_a_half_specified_or_misspelled_caption_style)
{
    Musi_Project p=fixture(),decoded;size_t n;char*json=encode(&p,&n);if(!json)return;
    char*style=strstr(json,",\"caption_style\":{");REQUIRE_TRUE(style!=NULL);
    char*end=strstr(style,",\"output\":");REQUIRE_TRUE(end!=NULL);
    size_t prefix=(size_t)(style-json),suffix=n-(size_t)(end-json);
    char*buffer=malloc(prefix+suffix+512);REQUIRE_TRUE(buffer!=NULL);

    // Present but incomplete. Filling the gaps from the defaults would silently
    // mix a shipped value into a style the author thought they had specified.
    const char *partial=",\"caption_style\":{\"face\":\"alegreya\",\"size_scale\":0.05}";
    memcpy(buffer,json,prefix);strcpy(buffer+prefix,partial);
    memcpy(buffer+prefix+strlen(partial),end,suffix);buffer[prefix+strlen(partial)+suffix]=0;
    EXPECT_TRUE(musi_project_json_deserialize(&decoded,buffer,strlen(buffer))==
                MUSI_PROJECT_IO_ERROR_MISSING_FIELD);

    // Colour spellings the format does not admit. Two ways to write one colour
    // would break the exact-identity promise the codec makes everywhere else.
    static const char *const bad_colours[]={"\"#ffffffff\"","\"FFFFFFFF\"","\"ffff\"","4294967295"};
    for(size_t i=0;i<sizeof(bad_colours)/sizeof(bad_colours[0]);++i){
        memcpy(buffer,json,prefix);
        int written=snprintf(buffer+prefix,512,
            ",\"caption_style\":{\"face\":\"alegreya\",\"box\":\"plate\",\"anchor\":\"bottom_center\","
            "\"size_scale\":0.047,\"margin_scale\":0.065,\"width_scale\":0.82,"
            "\"text_rgba\":%s,\"box_rgba\":\"000000b8\",\"font\":null}",bad_colours[i]);
        REQUIRE_TRUE(written>0&&written<512);
        size_t used=prefix+(size_t)written;
        memcpy(buffer+used,end,suffix);buffer[used+suffix]=0;
        EXPECT_TRUE(musi_project_json_deserialize(&decoded,buffer,used+suffix)!=
                    MUSI_PROJECT_IO_OK);
    }
    // The same shape with a legal colour must parse, or the loop above would
    // pass for the wrong reason.
    memcpy(buffer,json,prefix);
    int written=snprintf(buffer+prefix,512,
        ",\"caption_style\":{\"face\":\"alegreya\",\"box\":\"plate\",\"anchor\":\"bottom_center\","
        "\"size_scale\":0.047,\"margin_scale\":0.065,\"width_scale\":0.82,"
        "\"text_rgba\":\"ffffffff\",\"box_rgba\":\"000000b8\",\"font\":null}");
    REQUIRE_TRUE(written>0&&written<512);
    memcpy(buffer+prefix+(size_t)written,end,suffix);buffer[prefix+(size_t)written+suffix]=0;
    EXPECT_TRUE(musi_project_json_deserialize(&decoded,buffer,prefix+(size_t)written+suffix)==
                MUSI_PROJECT_IO_OK);
    free(buffer);free(json);
}

TEST(project_io_rejects_a_caption_style_out_of_range_or_disagreeing_with_its_font)
{
    Musi_Project p=fixture();
    p.caption_style.face=MUSI_CAPTION_FACE_ALEGREYA;p.caption_style.font.present=false;
    p.caption_style.font.path[0]=0;p.caption_style.font.sha256[0]=0;p.caption_style.font.family[0]=0;
    p.caption_style.font.licence_path[0]=0;p.caption_style.font.licence_sha256[0]=0;
    p.caption_style.font.licence_name[0]=0;
    REQUIRE_TRUE(musi_project_validate(&p).error==MUSI_PROJECT_VALID);

    // A face with no asset must carry no residue of one, licence included.
    Musi_Project residue=p;strcpy(residue.caption_style.font.licence_name,"OFL-1.1");
    EXPECT_TRUE(musi_project_validate(&residue).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);

    // An imported face with no asset, and an asset with no imported face, are
    // both a project whose captions cannot be reproduced from the file.
    Musi_Project bad=p;bad.caption_style.face=MUSI_CAPTION_FACE_IMPORTED;
    EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);
    bad=p;bad.caption_style.font.present=true;strcpy(bad.caption_style.font.path,"a.ttf");
    hash(bad.caption_style.font.sha256,'e');strcpy(bad.caption_style.font.family,"A");
    EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);

    static const double bad_sizes[]={0.0,0.011,0.31,1.0};
    for(size_t i=0;i<sizeof(bad_sizes)/sizeof(bad_sizes[0]);++i){
        bad=p;bad.caption_style.size_scale=bad_sizes[i];
        EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);
    }
    bad=p;bad.caption_style.margin_scale=0.5;
    EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);
    bad=p;bad.caption_style.width_scale=0.1;
    EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);
    bad=p;bad.caption_style.size_scale=1.0/0.0;
    EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);
    bad=p;bad.caption_style.anchor=(Musi_Caption_Anchor)MUSI_CAPTION_ANCHOR_COUNT;
    EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);
}

TEST(project_io_requires_a_bundled_font_licence_to_be_path_digest_and_name_together)
{
    Musi_Project p=fixture();
    REQUIRE_TRUE(musi_project_validate(&p).error==MUSI_PROJECT_VALID);

    // A licence with no digest cannot be verified before a recipient is shown
    // it; a digest with no path describes nothing at all.
    Musi_Project bad=p;bad.caption_style.font.licence_sha256[0]=0;
    EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);
    bad=p;bad.caption_style.font.licence_path[0]=0;
    EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);
    bad=p;bad.caption_style.font.licence_name[0]=0;
    EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);
    bad=p;memset(bad.caption_style.font.licence_sha256,'z',
                 sizeof(bad.caption_style.font.licence_sha256)-1);
    EXPECT_TRUE(musi_project_validate(&bad).error==MUSI_PROJECT_ERROR_CAPTION_STYLE);

    // A face the user imported from their own disk carries no licence we could
    // honestly assert, and that is a valid project, not a broken one.
    Musi_Project own=p;own.caption_style.font.licence_path[0]=0;
    own.caption_style.font.licence_sha256[0]=0;own.caption_style.font.licence_name[0]=0;
    EXPECT_TRUE(musi_project_validate(&own).error==MUSI_PROJECT_VALID);
}


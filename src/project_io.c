#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif

#include "project_io.h"
#include "sha256.h"
#include <locale.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

typedef struct { char *out; size_t cap, used; bool failed; } Writer;
static bool valid_utf8(const char *s)
{
    const unsigned char *p=(const unsigned char*)s;
    while(*p){unsigned c=*p++;if(c<0x80)continue;int n=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:-1;if(n<0)return false;uint32_t cp=c&((1u<<(6-n))-1);for(int i=0;i<n;++i){unsigned d=*p++;if((d&0xc0)!=0x80)return false;cp=(cp<<6)|(d&63);}if((n==2&&cp<0x800)||(n==3&&cp<0x10000)||(cp>=0xd800&&cp<=0xdfff)||cp>0x10ffff)return false;}return true;
}
static void bytes(Writer *w, const char *s, size_t n)
{
    if (w->out && w->used + n < w->cap) memcpy(w->out + w->used, s, n);
    else if (w->out) w->failed = true;
    w->used += n;
}
static void lit(Writer *w, const char *s) { bytes(w, s, strlen(s)); }
static void u64(Writer *w, uint64_t v)
{ char b[32]; int n=snprintf(b,sizeof(b),"%llu",(unsigned long long)v); bytes(w,b,(size_t)n); }
static void real(Writer *w, double v)
{
    char b[64]; int n=snprintf(b,sizeof(b),"%.17g",v);
    const char *decimal=localeconv()->decimal_point;
    if (decimal && strcmp(decimal,".") && decimal[0]) {
        char *at=strstr(b,decimal); if (at) { size_t dl=strlen(decimal); *at='.';
            if (dl>1) { memmove(at+1,at+dl,strlen(at+dl)+1); n-=(int)dl-1; } }
    }
    bytes(w,b,(size_t)n);
}
static void string(Writer *w, const char *s)
{
    if (!valid_utf8(s)) { w->failed=true; return; }
    lit(w,"\"");
    for (const unsigned char *p=(const unsigned char*)s; *p; ++p) {
        switch (*p) {
        case '"': lit(w,"\\\""); break; case '\\': lit(w,"\\\\"); break;
        case '\b': lit(w,"\\b"); break; case '\f': lit(w,"\\f"); break;
        case '\n': lit(w,"\\n"); break; case '\r': lit(w,"\\r"); break;
        case '\t': lit(w,"\\t"); break;
        default:
            if (*p<0x20) { char b[7]; snprintf(b,sizeof(b),"\\u%04x",*p); lit(w,b); }
            else bytes(w,(const char*)p,1);
            break;
        }
    }
    lit(w,"\"");
}
#define KEY(name) lit(w,"\"" name "\":")
static void mapping_write(Writer*w,const Musi_Parameter_Mapping*m)
{
    lit(w,"{\"parameter\":");string(w,m->parameter);lit(w,",\"source\":");string(w,musi_analysis_source_name(m->source));
    lit(w,",\"band_index\":");u64(w,m->band_index);lit(w,",\"input_min\":");real(w,m->input_min);
    lit(w,",\"input_max\":");real(w,m->input_max);lit(w,",\"output_min\":");real(w,m->output_min);
    lit(w,",\"output_max\":");real(w,m->output_max);lit(w,",\"interpolation\":");string(w,musi_interpolation_name(m->interpolation));
    lit(w,",\"clamp\":");lit(w,m->clamp?"true":"false");lit(w,"}");
}
static void events_write(Writer*w,const Event_Timeline*timeline)
{
    static const char*event_names[]={"lyric","semantic","cue","custom"};
    lit(w,"[");
    for(size_t i=0;i<timeline->count;++i){const Event_Record*e=&timeline->events[i];if(i)lit(w,",");lit(w,"{\"timestamp_seconds\":");real(w,e->timestamp_seconds);lit(w,",\"id\":");u64(w,e->id);lit(w,",\"type\":");string(w,event_names[e->type-EVENT_TYPE_LYRIC]);lit(w,",\"values\":[");for(size_t j=0;j<e->value_count;++j){if(j)lit(w,",");real(w,e->values[j]);}lit(w,"]}");}
    lit(w,"]");
}
static void project_write(Writer*w,const Musi_Project*p)
{
    lit(w,"{\"schema_version\":\"musializer.project/v1\",\"metadata\":{");
    lit(w,"\"project_id\":");string(w,p->metadata.project_id);lit(w,",\"title\":");string(w,p->metadata.title);
    lit(w,",\"author\":");string(w,p->metadata.author);lit(w,",\"created_utc\":");string(w,p->metadata.created_utc);
    lit(w,",\"modified_utc\":");string(w,p->metadata.modified_utc);lit(w,",\"application_version\":");string(w,p->metadata.application_version);
    lit(w,"},\"audio\":{\"mode\":");string(w,musi_asset_mode_name(p->audio.mode));lit(w,",\"path\":");string(w,p->audio.path);
    lit(w,",\"sha256\":");string(w,p->audio.sha256);lit(w,",\"duration_seconds\":");real(w,p->audio.duration_seconds);
    lit(w,",\"sample_rate\":");u64(w,p->audio.sample_rate);lit(w,",\"channels\":");u64(w,p->audio.channels);
    lit(w,"},\"ascii_image\":");
    if(p->ascii_image.present){lit(w,"{\"path\":");string(w,p->ascii_image.path);lit(w,",\"sha256\":");string(w,p->ascii_image.sha256);lit(w,",\"columns\":");u64(w,p->ascii_image.columns);lit(w,",\"rows\":");u64(w,p->ascii_image.rows);lit(w,"}");}else lit(w,"null");
    lit(w,",\"output\":{\"width\":");u64(w,p->output.width);lit(w,",\"height\":");u64(w,p->output.height);
    lit(w,",\"fps_numerator\":");u64(w,p->output.fps_numerator);lit(w,",\"fps_denominator\":");u64(w,p->output.fps_denominator);
    lit(w,",\"start_seconds\":");real(w,p->output.start_seconds);lit(w,",\"end_seconds\":");real(w,p->output.end_seconds);
    lit(w,",\"format\":");string(w,musi_output_format_name(p->output.format));lit(w,",\"quality\":");string(w,musi_output_quality_name(p->output.quality));lit(w,"},\"deterministic_seed\":");u64(w,p->deterministic_seed);
    lit(w,",\"scenes\":["); for(size_t i=0;i<p->scene_count;++i){const Musi_Scene_Entry*s=&p->scenes[i];if(i)lit(w,",");
        lit(w,"{\"instance_id\":");u64(w,s->instance_id);lit(w,",\"scene_type\":");string(w,s->scene_type);
        lit(w,",\"enabled\":");lit(w,s->enabled?"true":"false");lit(w,",\"start_seconds\":");real(w,s->start_seconds);
        lit(w,",\"end_seconds\":");real(w,s->end_seconds);lit(w,",\"opacity\":");real(w,s->opacity);
        lit(w,",\"blend_mode\":");string(w,musi_blend_mode_name(s->blend_mode));lit(w,",\"mappings\":[");
        for(size_t j=0;j<s->mapping_count;++j){if(j)lit(w,",");mapping_write(w,&s->mappings[j]);}lit(w,"]}");}
    lit(w,"],\"cues\":[");for(size_t i=0;i<p->cue_count;++i){const Musi_Parameter_Cue*c=&p->cues[i];if(i)lit(w,",");
        lit(w,"{\"cue_id\":");u64(w,c->cue_id);lit(w,",\"target_scene_id\":");u64(w,c->target_scene_id);
        lit(w,",\"parameter\":");string(w,c->parameter);lit(w,",\"start_seconds\":");real(w,c->start_seconds);
        lit(w,",\"end_seconds\":");real(w,c->end_seconds);lit(w,",\"from_value\":");real(w,c->from_value);
        lit(w,",\"to_value\":");real(w,c->to_value);lit(w,",\"interpolation\":");string(w,musi_interpolation_name(c->interpolation));lit(w,"}");}
    lit(w,"],\"analysis_lanes\":[");for(size_t i=0;i<p->analysis_lane_count;++i){const Musi_Analysis_Lane_Reference*l=&p->analysis_lanes[i];if(i)lit(w,",");
        lit(w,"{\"kind\":");string(w,musi_analysis_lane_kind_name(l->kind));lit(w,",\"path\":");string(w,l->path);
        lit(w,",\"sha256\":");string(w,l->sha256);lit(w,",\"audio_sha256\":");string(w,l->audio_sha256);
        lit(w,",\"provenance\":{\"adapter\":");string(w,l->provenance.adapter);lit(w,",\"adapter_version\":");string(w,l->provenance.adapter_version);
        lit(w,",\"schema_version\":");string(w,l->provenance.schema_version);lit(w,",\"model\":");string(w,l->provenance.model);
        lit(w,",\"provider\":");string(w,l->provenance.provider);lit(w,",\"prompt_version\":");string(w,l->provenance.prompt_version);lit(w,"}}");}
    lit(w,"],\"lyrics\":{\"next_id\":");u64(w,p->lyrics.next_id);lit(w,",\"cues\":[");
    for(size_t i=0;i<p->lyrics.count;++i){const Lyric_Cue*c=&p->lyrics.cues[i];if(i)lit(w,",");lit(w,"{\"id\":");u64(w,c->id);lit(w,",\"start_seconds\":");real(w,c->start_seconds);lit(w,",\"end_seconds\":");real(w,c->end_seconds);lit(w,",\"text\":");string(w,c->text);lit(w,"}");}
    lit(w,"]},\"scene_switches\":{\"enabled\":");lit(w,p->scene_switches.enabled?"true":"false");lit(w,",\"cues\":[");
    for(size_t i=0;i<p->scene_switches.count;++i){const Musi_Scene_Switch_Suggestion*c=&p->scene_switches.cues[i];if(i)lit(w,",");lit(w,"{\"id\":");u64(w,c->id);lit(w,",\"start_seconds\":");real(w,c->start_seconds);lit(w,",\"end_seconds\":");real(w,c->end_seconds);lit(w,",\"scene_name\":");string(w,c->scene_name);lit(w,",\"strength\":");real(w,c->strength);lit(w,",\"settings\":[");for(size_t j=0;j<c->setting_count;++j){if(j)lit(w,",");real(w,c->settings[j]);}lit(w,"]}");}
    lit(w,"]},\"scene_presets\":[");for(size_t i=0;i<p->scene_preset_count;++i){const Musi_Scene_Preset*s=&p->scene_presets[i];if(i)lit(w,",");lit(w,"{\"id\":");u64(w,s->id);lit(w,",\"scene_name\":");string(w,s->scene_name);lit(w,",\"name\":");string(w,s->name);lit(w,",\"settings\":[");for(size_t j=0;j<s->setting_count;++j){if(j)lit(w,",");real(w,s->settings[j]);}lit(w,"]}");}
    lit(w,"],\"semantic_events\":");events_write(w,&p->semantic_events);
    lit(w,",\"manual_events\":");events_write(w,&p->manual_events);lit(w,"}");
}

typedef struct { const char *p,*end; Musi_Project_Io_Result error; } Parser;
static void ws(Parser*x){while(x->p<x->end&&(*x->p==' '||*x->p=='\n'||*x->p=='\r'||*x->p=='\t'))++x->p;}
static bool take(Parser*x,char c){ws(x);if(x->p<x->end&&*x->p==c){++x->p;return true;}x->error=MUSI_PROJECT_IO_ERROR_SYNTAX;return false;}
static bool utf8_append(char*out,size_t cap,size_t*n,uint32_t cp)
{
    unsigned char b[4];size_t k;if(cp<=0x7f){b[0]=cp;k=1;}else if(cp<=0x7ff){b[0]=0xc0|(cp>>6);b[1]=0x80|(cp&63);k=2;}
    else if(cp<=0xffff&&!(cp>=0xd800&&cp<=0xdfff)){b[0]=0xe0|(cp>>12);b[1]=0x80|((cp>>6)&63);b[2]=0x80|(cp&63);k=3;}
    else if(cp<=0x10ffff){b[0]=0xf0|(cp>>18);b[1]=0x80|((cp>>12)&63);b[2]=0x80|((cp>>6)&63);b[3]=0x80|(cp&63);k=4;}else return false;
    if(*n+k>=cap)return false;
    memcpy(out+*n,b,k);*n+=k;return true;
}
static int hex4(Parser*x,uint32_t*out){uint32_t v=0;for(int i=0;i<4;++i){if(x->p>=x->end)return 0;char c=*x->p++;int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(d<0)return 0;v=v*16+(unsigned)d;}*out=v;return 1;}
static bool jstring(Parser*x,char*out,size_t cap)
{
    ws(x);if(x->p>=x->end||*x->p++!='"'){x->error=MUSI_PROJECT_IO_ERROR_SYNTAX;return false;}size_t n=0;
    while(x->p<x->end&&*x->p!='"'){unsigned char c=(unsigned char)*x->p++;uint32_t cp=c;
        if(c=='\\'){if(x->p>=x->end)goto bad;char e=*x->p++;if(e=='"'||e=='\\'||e=='/')cp=e;else if(e=='b')cp=8;else if(e=='f')cp=12;else if(e=='n')cp=10;else if(e=='r')cp=13;else if(e=='t')cp=9;else if(e=='u'){if(!hex4(x,&cp))goto bad;if(cp>=0xd800&&cp<=0xdbff){uint32_t lo;if(x->end-x->p<2||x->p[0]!='\\'||x->p[1]!='u')goto bad;x->p+=2;if(!hex4(x,&lo)||lo<0xdc00||lo>0xdfff)goto bad;cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);}else if(cp>=0xdc00&&cp<=0xdfff)goto bad;}else goto bad;
        }else if(c<0x20)goto bad;else if(c>=0x80){int cont=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:-1;if(cont<0||x->end-x->p<cont)goto bad;cp=c&((1u<<(6-cont))-1);for(int i=0;i<cont;++i){unsigned char d=*x->p++;if((d&0xc0)!=0x80)goto bad;cp=(cp<<6)|(d&63);}if((cont==2&&cp<0x800)||(cont==3&&cp<0x10000)||(cp>=0xd800&&cp<=0xdfff)||cp>0x10ffff)goto bad;}
        if(cp==0||!utf8_append(out,cap,&n,cp)){x->error=MUSI_PROJECT_IO_ERROR_STRING;return false;}}
    if(x->p>=x->end)goto bad;
    ++x->p;out[n]=0;return true;
bad:x->error=MUSI_PROJECT_IO_ERROR_STRING;return false;
}
static bool number_token(Parser*x,const char**start,size_t*len,bool integer)
{
    ws(x);const char*s=x->p;if(x->p<x->end&&*x->p=='-')++x->p;if(x->p>=x->end)return false;
    if(*x->p=='0')++x->p;else if(*x->p>='1'&&*x->p<='9'){while(x->p<x->end&&*x->p>='0'&&*x->p<='9')++x->p;}else return false;
    if(!integer&&x->p<x->end&&*x->p=='.'){++x->p;if(x->p>=x->end||*x->p<'0'||*x->p>'9')return false;while(x->p<x->end&&*x->p>='0'&&*x->p<='9')++x->p;}
    if(!integer&&x->p<x->end&&(*x->p=='e'||*x->p=='E')){++x->p;if(x->p<x->end&&(*x->p=='+'||*x->p=='-'))++x->p;if(x->p>=x->end||*x->p<'0'||*x->p>'9')return false;while(x->p<x->end&&*x->p>='0'&&*x->p<='9')++x->p;}
    *start=s;*len=(size_t)(x->p-s);return true;
}
static bool ju64(Parser*x,uint64_t*out)
{const char*s;size_t n;if(!number_token(x,&s,&n,true)||*s=='-'){x->error=MUSI_PROJECT_IO_ERROR_NUMBER;return false;}uint64_t v=0;for(size_t i=0;i<n;++i){unsigned d=s[i]-'0';if(v>(UINT64_MAX-d)/10){x->error=MUSI_PROJECT_IO_ERROR_NUMBER;return false;}v=v*10+d;}*out=v;return true;}
static bool jdouble(Parser*x,double*out)
{const char*s;size_t n;if(!number_token(x,&s,&n,false)||n>=64){x->error=MUSI_PROJECT_IO_ERROR_NUMBER;return false;}char b[96];memcpy(b,s,n);b[n]=0;const char*dp=localeconv()->decimal_point;if(dp&&strcmp(dp,".")&&dp[0]){char*dot=strchr(b,'.');if(dot){size_t dl=strlen(dp),tail=strlen(dot+1);if(n+dl>=sizeof(b))return false;memmove(dot+dl,dot+1,tail+1);memcpy(dot,dp,dl);}}char*e;double v=strtod(b,&e);if(*e||!isfinite(v)){x->error=MUSI_PROJECT_IO_ERROR_NUMBER;return false;}*out=v;return true;}
static bool jbool(Parser*x,bool*out){ws(x);if(x->end-x->p>=4&&!memcmp(x->p,"true",4)){x->p+=4;*out=true;return true;}if(x->end-x->p>=5&&!memcmp(x->p,"false",5)){x->p+=5;*out=false;return true;}x->error=MUSI_PROJECT_IO_ERROR_SYNTAX;return false;}
static int field_index(const char*k,const char*const*names,size_t count){for(size_t i=0;i<count;++i)if(!strcmp(k,names[i]))return(int)i;return-1;}
static bool enum_string(Parser*x,const char*const*names,size_t count,int*out){char b[80];if(!jstring(x,b,sizeof(b)))return false;int i=field_index(b,names,count);if(i<0){x->error=MUSI_PROJECT_IO_ERROR_SCHEMA;return false;}*out=i;return true;}
static bool member(Parser*x,char*k,size_t cap,bool*first)
{ws(x);if(*first){*first=false;}else if(!take(x,','))return false;if(!jstring(x,k,cap)||!take(x,':'))return false;return true;}
#define SEEN(bit) do{if(mask&(UINT64_C(1)<<(bit))){x->error=MUSI_PROJECT_IO_ERROR_DUPLICATE_FIELD;return false;}mask|=UINT64_C(1)<<(bit);}while(0)
#define UNKNOWN() do{x->error=MUSI_PROJECT_IO_ERROR_UNKNOWN_FIELD;return false;}while(0)
static const char* modes[]={"imported","referenced"},*formats[]={"mp4_h264","mkv_h264","webm_vp9","mov_prores","png_sequence"},*qualities[]={"balanced","high","master"},*blends[]={"normal","add","multiply","screen"},*sources[]={"rms","peak","spectral_flux","beat_phase","band"},*interps[]={"step","linear","smoothstep","ease_in","ease_out"},*kinds[]={"measured_signal","lyric_timing","semantic_score"};

static bool parse_mapping(Parser*x,Musi_Parameter_Mapping*m)
{static const char*names[]={"parameter","source","band_index","input_min","input_max","output_min","output_max","interpolation","clamp"};uint64_t mask=0,v;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,9);if(f<0)UNKNOWN();SEEN(f);int e;switch(f){case 0:if(!jstring(x,m->parameter,sizeof(m->parameter)))return false;break;case 1:if(!enum_string(x,sources,5,&e))return false;m->source=e;break;case 2:if(!ju64(x,&v)||v>UINT16_MAX)return false;m->band_index=(uint16_t)v;break;case 3:if(!jdouble(x,&m->input_min))return false;break;case 4:if(!jdouble(x,&m->input_max))return false;break;case 5:if(!jdouble(x,&m->output_min))return false;break;case 6:if(!jdouble(x,&m->output_max))return false;break;case 7:if(!enum_string(x,interps,5,&e))return false;m->interpolation=e;break;case 8:if(!jbool(x,&m->clamp))return false;}}
 if(mask!=0x1ff){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_scene(Parser*x,Musi_Scene_Entry*s)
{static const char*names[]={"instance_id","scene_type","enabled","start_seconds","end_seconds","opacity","blend_mode","mappings"};uint64_t mask=0,v;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,8);if(f<0)UNKNOWN();SEEN(f);int e;switch(f){case 0:if(!ju64(x,&s->instance_id))return false;break;case 1:if(!jstring(x,s->scene_type,sizeof(s->scene_type)))return false;break;case 2:if(!jbool(x,&s->enabled))return false;break;case 3:if(!jdouble(x,&s->start_seconds))return false;break;case 4:if(!jdouble(x,&s->end_seconds))return false;break;case 5:if(!jdouble(x,&s->opacity))return false;break;case 6:if(!enum_string(x,blends,4,&e))return false;s->blend_mode=e;break;case 7:if(!take(x,'['))return false;while(1){ws(x);if(x->p<x->end&&*x->p==']'){++x->p;break;}if(s->mapping_count&& !take(x,','))return false;if(s->mapping_count>=MUSI_PROJECT_MAX_MAPPINGS_PER_SCENE){x->error=MUSI_PROJECT_IO_ERROR_CAPACITY;return false;}if(!parse_mapping(x,&s->mappings[s->mapping_count++]))return false;}break;}(void)v;}if(mask!=0xff){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_cue(Parser*x,Musi_Parameter_Cue*c)
{static const char*names[]={"cue_id","target_scene_id","parameter","start_seconds","end_seconds","from_value","to_value","interpolation"};uint64_t mask=0;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,8);if(f<0)UNKNOWN();SEEN(f);int e;switch(f){case 0:if(!ju64(x,&c->cue_id))return false;break;case 1:if(!ju64(x,&c->target_scene_id))return false;break;case 2:if(!jstring(x,c->parameter,sizeof(c->parameter)))return false;break;case 3:if(!jdouble(x,&c->start_seconds))return false;break;case 4:if(!jdouble(x,&c->end_seconds))return false;break;case 5:if(!jdouble(x,&c->from_value))return false;break;case 6:if(!jdouble(x,&c->to_value))return false;break;case 7:if(!enum_string(x,interps,5,&e))return false;c->interpolation=e;break;}}if(mask!=0xff){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}

static bool parse_metadata(Parser*x,Musi_Project_Metadata*m)
{static const char*names[]={"project_id","title","author","created_utc","modified_utc","application_version"};uint64_t mask=0;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,6);if(f<0)UNKNOWN();SEEN(f);char*o=f==0?m->project_id:f==1?m->title:f==2?m->author:f==3?m->created_utc:f==4?m->modified_utc:m->application_version;size_t cap=f==0?sizeof(m->project_id):f==1||f==2?sizeof(m->title):f==3||f==4?sizeof(m->created_utc):sizeof(m->application_version);if(!jstring(x,o,cap))return false;}if((mask&0x23)!=0x23){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_audio(Parser*x,Musi_Audio_Asset*a)
{static const char*names[]={"mode","path","sha256","duration_seconds","sample_rate","channels"};uint64_t mask=0,v;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,6);if(f<0)UNKNOWN();SEEN(f);int e;switch(f){case 0:if(!enum_string(x,modes,2,&e))return false;a->mode=e;break;case 1:if(!jstring(x,a->path,sizeof(a->path)))return false;break;case 2:if(!jstring(x,a->sha256,sizeof(a->sha256)))return false;break;case 3:if(!jdouble(x,&a->duration_seconds))return false;break;case 4:if(!ju64(x,&v)||v>UINT32_MAX){x->error=MUSI_PROJECT_IO_ERROR_NUMBER;return false;}a->sample_rate=(uint32_t)v;break;case 5:if(!ju64(x,&v)||v>UINT16_MAX){x->error=MUSI_PROJECT_IO_ERROR_NUMBER;return false;}a->channels=(uint16_t)v;break;}}if(mask!=0x3f){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_ascii_image(Parser*x,Musi_Ascii_Image_Asset*a)
{ws(x);if(x->end-x->p>=4&&!memcmp(x->p,"null",4)){x->p+=4;return true;}static const char*names[]={"path","sha256","columns","rows"};uint64_t mask=0,v;bool first=true;char k[80];if(!take(x,'{'))return false;a->present=true;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,4);if(f<0)UNKNOWN();SEEN(f);if(f==0){if(!jstring(x,a->path,sizeof(a->path)))return false;}else if(f==1){if(!jstring(x,a->sha256,sizeof(a->sha256)))return false;}else if(f==2){if(!ju64(x,&v)||v>UINT32_MAX){x->error=MUSI_PROJECT_IO_ERROR_NUMBER;return false;}a->columns=(uint32_t)v;}else{if(!ju64(x,&v)||v>UINT32_MAX){x->error=MUSI_PROJECT_IO_ERROR_NUMBER;return false;}a->rows=(uint32_t)v;}}if(mask!=15){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_output(Parser*x,Musi_Output_Settings*o)
{static const char*names[]={"width","height","fps_numerator","fps_denominator","start_seconds","end_seconds","format","quality"};uint64_t mask=0,v;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,8);if(f<0)UNKNOWN();SEEN(f);int e;if(f<4){if(!ju64(x,&v)||v>UINT32_MAX){x->error=MUSI_PROJECT_IO_ERROR_NUMBER;return false;}if(f==0)o->width=v;else if(f==1)o->height=v;else if(f==2)o->fps_numerator=v;else o->fps_denominator=v;}else if(f==4){if(!jdouble(x,&o->start_seconds))return false;}else if(f==5){if(!jdouble(x,&o->end_seconds))return false;}else if(f==6){if(!enum_string(x,formats,5,&e))return false;o->format=e;}else{if(!enum_string(x,qualities,3,&e))return false;o->quality=e;}}if((mask&UINT64_C(0x7f))!=UINT64_C(0x7f)){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_provenance(Parser*x,Musi_Analysis_Provenance*p)
{static const char*names[]={"adapter","adapter_version","schema_version","model","provider","prompt_version"};uint64_t mask=0;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,6);if(f<0)UNKNOWN();SEEN(f);char*o=f==0?p->adapter:f==1?p->adapter_version:f==2?p->schema_version:f==3?p->model:f==4?p->provider:p->prompt_version;size_t cap=f==0?sizeof(p->adapter):f==1||f==2||f==5?sizeof(p->adapter_version):sizeof(p->model);if(!jstring(x,o,cap))return false;}if((mask&7)!=7){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_lane(Parser*x,Musi_Analysis_Lane_Reference*l)
{static const char*names[]={"kind","path","sha256","audio_sha256","provenance"};uint64_t mask=0;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,5);if(f<0)UNKNOWN();SEEN(f);int e;if(f==0){if(!enum_string(x,kinds,3,&e))return false;l->kind=e;}else if(f==1){if(!jstring(x,l->path,sizeof(l->path)))return false;}else if(f==2){if(!jstring(x,l->sha256,sizeof(l->sha256)))return false;}else if(f==3){if(!jstring(x,l->audio_sha256,sizeof(l->audio_sha256)))return false;}else if(!parse_provenance(x,&l->provenance))return false;}if(mask!=0x1f){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}

static bool parse_lyric(Parser*x,Lyric_Cue*c)
{static const char*names[]={"id","start_seconds","end_seconds","text"};uint64_t mask=0;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,4);if(f<0)UNKNOWN();SEEN(f);if(f==0){if(!ju64(x,&c->id))return false;}else if(f==1){if(!jdouble(x,&c->start_seconds))return false;}else if(f==2){if(!jdouble(x,&c->end_seconds))return false;}else if(!jstring(x,c->text,sizeof(c->text)))return false;}if(mask!=15){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_lyrics(Parser*x,Lyrics_Document*d)
{static const char*names[]={"next_id","cues"};uint64_t mask=0;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,2);if(f<0)UNKNOWN();SEEN(f);if(f==0){if(!ju64(x,&d->next_id))return false;}else{if(!take(x,'['))return false;while(1){ws(x);if(x->p<x->end&&*x->p==']'){++x->p;break;}if(d->count&&!take(x,','))return false;if(d->count>=LYRICS_CUE_CAPACITY){x->error=MUSI_PROJECT_IO_ERROR_CAPACITY;return false;}if(!parse_lyric(x,&d->cues[d->count++]))return false;}}}if(mask!=3){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_float_array(Parser*x,float*values,size_t capacity,size_t*count)
{if(!take(x,'['))return false;while(1){ws(x);if(x->p<x->end&&*x->p==']'){++x->p;break;}if(*count&&!take(x,','))return false;if(*count>=capacity){x->error=MUSI_PROJECT_IO_ERROR_CAPACITY;return false;}double value;if(!jdouble(x,&value)||value>FLT_MAX||value<-FLT_MAX)return false;values[(*count)++]=(float)value;}return true;}
static bool parse_switch(Parser*x,Musi_Scene_Switch_Suggestion*c)
{static const char*names[]={"id","start_seconds","end_seconds","scene_name","strength","settings"};uint64_t mask=0;bool first=true;char k[80];double strength;if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,6);if(f<0)UNKNOWN();SEEN(f);if(f==0){if(!ju64(x,&c->id))return false;}else if(f==1){if(!jdouble(x,&c->start_seconds))return false;}else if(f==2){if(!jdouble(x,&c->end_seconds))return false;}else if(f==3){if(!jstring(x,c->scene_name,sizeof(c->scene_name)))return false;}else if(f==4){if(!jdouble(x,&strength)||strength>FLT_MAX||strength<-FLT_MAX)return false;c->strength=(float)strength;}else if(!parse_float_array(x,c->settings,SCENE_SETTINGS_MAX_CONTROLS,&c->setting_count))return false;}if((mask&31)!=31){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_switches(Parser*x,Musi_Scene_Switch_Suggestions*s)
{static const char*names[]={"enabled","cues"};uint64_t mask=0;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,2);if(f<0)UNKNOWN();SEEN(f);if(f==0){if(!jbool(x,&s->enabled))return false;}else{if(!take(x,'['))return false;while(1){ws(x);if(x->p<x->end&&*x->p==']'){++x->p;break;}if(s->count&&!take(x,','))return false;if(s->count>=SCENE_SWITCH_CAPACITY){x->error=MUSI_PROJECT_IO_ERROR_CAPACITY;return false;}if(!parse_switch(x,&s->cues[s->count++]))return false;}}}if(mask!=3){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_event(Parser*x,Event_Record*e)
{static const char*names[]={"timestamp_seconds","id","type","values"};static const char*types[]={"lyric","semantic","cue","custom"};uint64_t mask=0;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,4);if(f<0)UNKNOWN();SEEN(f);if(f==0){if(!jdouble(x,&e->timestamp_seconds))return false;}else if(f==1){if(!ju64(x,&e->id))return false;}else if(f==2){int type;if(!enum_string(x,types,4,&type))return false;e->type=(uint32_t)type+EVENT_TYPE_LYRIC;}else{if(!take(x,'['))return false;while(1){ws(x);if(x->p<x->end&&*x->p==']'){++x->p;break;}if(e->value_count&&!take(x,','))return false;if(e->value_count>=EVENT_VALUE_CAPACITY){x->error=MUSI_PROJECT_IO_ERROR_CAPACITY;return false;}double value;if(!jdouble(x,&value)||value>FLT_MAX||value<-FLT_MAX)return false;e->values[e->value_count++]=(float)value;}}}if(mask!=15){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}
static bool parse_events(Parser*x,Event_Timeline*timeline)
{if(!take(x,'['))return false;while(1){ws(x);if(x->p<x->end&&*x->p==']'){++x->p;break;}if(timeline->count&&!take(x,','))return false;if(timeline->count>=EVENT_TIMELINE_CAPACITY){x->error=MUSI_PROJECT_IO_ERROR_CAPACITY;return false;}if(!parse_event(x,&timeline->events[timeline->count++]))return false;}return true;}

static bool parse_scene_preset(Parser*x,Musi_Scene_Preset*s)
{static const char*names[]={"id","scene_name","name","settings"};uint64_t mask=0;bool first=true;char k[80];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,4);if(f<0)UNKNOWN();SEEN(f);if(f==0){if(!ju64(x,&s->id))return false;}else if(f==1){if(!jstring(x,s->scene_name,sizeof(s->scene_name)))return false;}else if(f==2){if(!jstring(x,s->name,sizeof(s->name)))return false;}else if(!parse_float_array(x,s->settings,SCENE_SETTINGS_MAX_CONTROLS,&s->setting_count))return false;}if(mask!=15){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}return true;}

static bool parse_project(Parser*x,Musi_Project*p)
{static const char*names[]={"schema_version","metadata","audio","output","deterministic_seed","scenes","cues","analysis_lanes","lyrics","scene_switches","manual_events","semantic_events","scene_presets","ascii_image"};uint64_t mask=0;bool first=true;char k[80],version[64];if(!take(x,'{'))return false;while(1){ws(x);if(x->p<x->end&&*x->p=='}'){++x->p;break;}if(!member(x,k,sizeof(k),&first))return false;int f=field_index(k,names,14);if(f<0)UNKNOWN();SEEN(f);switch(f){case 0:if(!jstring(x,version,sizeof(version)))return false;if(strcmp(version,"musializer.project/v1")){x->error=MUSI_PROJECT_IO_ERROR_SCHEMA;return false;}p->schema_version=MUSI_PROJECT_SCHEMA_VERSION;break;case 1:if(!parse_metadata(x,&p->metadata))return false;break;case 2:if(!parse_audio(x,&p->audio))return false;break;case 3:if(!parse_output(x,&p->output))return false;break;case 4:if(!ju64(x,&p->deterministic_seed))return false;break;case 5:if(!take(x,'['))return false;while(1){ws(x);if(x->p<x->end&&*x->p==']'){++x->p;break;}if(p->scene_count&&!take(x,','))return false;if(p->scene_count>=MUSI_PROJECT_MAX_SCENES){x->error=MUSI_PROJECT_IO_ERROR_CAPACITY;return false;}if(!parse_scene(x,&p->scenes[p->scene_count++]))return false;}break;case 6:if(!take(x,'['))return false;while(1){ws(x);if(x->p<x->end&&*x->p==']'){++x->p;break;}if(p->cue_count&&!take(x,','))return false;if(p->cue_count>=MUSI_PROJECT_MAX_CUES){x->error=MUSI_PROJECT_IO_ERROR_CAPACITY;return false;}if(!parse_cue(x,&p->cues[p->cue_count++]))return false;}break;case 7:if(!take(x,'['))return false;while(1){ws(x);if(x->p<x->end&&*x->p==']'){++x->p;break;}if(p->analysis_lane_count&&!take(x,','))return false;if(p->analysis_lane_count>=MUSI_PROJECT_MAX_ANALYSIS_LANES){x->error=MUSI_PROJECT_IO_ERROR_CAPACITY;return false;}if(!parse_lane(x,&p->analysis_lanes[p->analysis_lane_count++]))return false;}break;case 8:if(!parse_lyrics(x,&p->lyrics))return false;break;case 9:if(!parse_switches(x,&p->scene_switches))return false;break;case 10:if(!parse_events(x,&p->manual_events))return false;break;case 11:if(!parse_events(x,&p->semantic_events))return false;break;case 12:if(!take(x,'['))return false;while(1){ws(x);if(x->p<x->end&&*x->p==']'){++x->p;break;}if(p->scene_preset_count&&!take(x,','))return false;if(p->scene_preset_count>=MUSI_PROJECT_MAX_SCENE_PRESETS){x->error=MUSI_PROJECT_IO_ERROR_CAPACITY;return false;}if(!parse_scene_preset(x,&p->scene_presets[p->scene_preset_count++]))return false;}break;case 13:if(!parse_ascii_image(x,&p->ascii_image))return false;break;}}
 if((mask&UINT64_C(0xff))!=UINT64_C(0xff)){x->error=MUSI_PROJECT_IO_ERROR_MISSING_FIELD;return false;}p->lyrics.duration_seconds=p->audio.duration_seconds;return true;}

Musi_Project_Io_Result musi_project_json_serialize(const Musi_Project*p,char*out,size_t cap,size_t*required)
{
    if(!p||!required)return MUSI_PROJECT_IO_ERROR_NULL;
    if(musi_project_validate(p).error!=MUSI_PROJECT_VALID)return MUSI_PROJECT_IO_ERROR_VALIDATION;
    Writer measure={0};project_write(&measure,p);*required=measure.used+1;
    if(measure.failed)return MUSI_PROJECT_IO_ERROR_STRING;
    if(!out||cap<*required)return MUSI_PROJECT_IO_ERROR_OUTPUT_TOO_SMALL;
    Writer w={.out=out,.cap=cap};project_write(&w,p);if(w.failed)return MUSI_PROJECT_IO_ERROR_OUTPUT_TOO_SMALL;out[w.used]=0;return MUSI_PROJECT_IO_OK;
}
Musi_Project_Io_Result musi_project_json_deserialize(Musi_Project*dest,const char*input,size_t size)
{
    if(!dest||!input)return MUSI_PROJECT_IO_ERROR_NULL;
    if(!size||size>MUSI_PROJECT_JSON_MAX_INPUT||memchr(input,0,size))return MUSI_PROJECT_IO_ERROR_INPUT_SIZE;
    Musi_Project*p=malloc(sizeof(*p));if(!p)return MUSI_PROJECT_IO_ERROR_ALLOCATION;musi_project_init(p);
    Parser x={.p=input,.end=input+size,.error=MUSI_PROJECT_IO_OK};bool ok=parse_project(&x,p);ws(&x);
    Musi_Project_Io_Result r=ok&&x.p==x.end?MUSI_PROJECT_IO_OK:(x.error?x.error:MUSI_PROJECT_IO_ERROR_SYNTAX);
    if(r==MUSI_PROJECT_IO_OK&&musi_project_validate(p).error!=MUSI_PROJECT_VALID)r=MUSI_PROJECT_IO_ERROR_VALIDATION;
    if(r==MUSI_PROJECT_IO_OK)memcpy(dest,p,sizeof(*dest));
    free(p);return r;
}
const char*musi_project_io_result_string(Musi_Project_Io_Result r)
{static const char*n[]={"ok","null argument","invalid input size","malformed JSON","unknown field","duplicate field","missing field","invalid or oversized string","invalid number","array capacity exceeded","schema mismatch","project validation failed","output buffer too small","allocation failed"};return r>=0&&(size_t)r<sizeof(n)/sizeof(n[0])?n[r]:"unknown project I/O error";}

static bool project_path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;
#ifdef _WIN32
    if (path[0] == '/' || path[0] == '\\') return true;
    return path[1] == ':' &&
           ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) &&
           (path[2] == '/' || path[2] == '\\');
#else
    return path[0] == '/';
#endif
}

static const char *project_last_separator(const char *path)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    return backslash != NULL && (slash == NULL || backslash > slash)
               ? backslash : slash;
#else
    return slash;
#endif
}

#ifdef _WIN32
static wchar_t *project_utf8_to_wide(const char *input)
{
    if (input == NULL) return NULL;
    int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       input, -1, NULL, 0);
    if (required <= 0 || (size_t)required > SIZE_MAX/sizeof(wchar_t)) return NULL;
    wchar_t *wide = malloc((size_t)required*sizeof(*wide));
    if (wide == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            input, -1, wide, required) != required) {
        free(wide);
        return NULL;
    }
    return wide;
}

static char *project_wide_to_utf8(const wchar_t *input)
{
    if (input == NULL) return NULL;
    int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                       input, -1, NULL, 0, NULL, NULL);
    if (required <= 0) return NULL;
    char *utf8 = malloc((size_t)required);
    if (utf8 == NULL) return NULL;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                            input, -1, utf8, required, NULL, NULL) != required) {
        free(utf8);
        return NULL;
    }
    return utf8;
}
#endif

static bool project_regular_file_exists(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = project_utf8_to_wide(path);
    if (wide == NULL) return false;
    DWORD attributes = GetFileAttributesW(wide);
    free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat status;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
#endif
}

static Musi_Project_Path_Result project_copy_resolved_path(
    const char *source, Musi_Project_Path_Result success,
    char *resolved, size_t capacity)
{
    size_t length = strlen(source);
    if (length >= capacity) return MUSI_PROJECT_PATH_ERROR_TOO_LONG;
    memcpy(resolved, source, length + 1);
    return success;
}

static bool project_path_character_is_separator(char character)
{
#ifdef _WIN32
    return character == '/' || character == '\\';
#else
    return character == '/';
#endif
}

Musi_Project_Path_Result musi_project_resolve_asset_path(
    const char *project_path, const char *asset_path,
    char *resolved, size_t capacity)
{
    if (project_path == NULL || project_path[0] == '\0' ||
        asset_path == NULL || asset_path[0] == '\0' ||
        resolved == NULL || capacity == 0) {
        return MUSI_PROJECT_PATH_ERROR_NULL;
    }

    if (project_path_is_absolute(asset_path)) {
        if (!project_regular_file_exists(asset_path)) {
            return MUSI_PROJECT_PATH_ERROR_NOT_FOUND;
        }
        return project_copy_resolved_path(asset_path,
                                          MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE,
                                          resolved, capacity);
    }

    const char *separator = project_last_separator(project_path);
    size_t directory_length = separator == NULL ? 1u :
                              (size_t)(separator - project_path);
    const char *directory = project_path;
    char separator_character = '/';
    if (separator == NULL) {
        directory = ".";
    } else {
        separator_character = *separator;
        // Preserve filesystem roots ("/file" and "C:\\file").
        if (directory_length == 0 ||
            (directory_length == 2 && project_path[1] == ':')) {
            ++directory_length;
        }
    }

    const char *relative_asset = asset_path;
    Musi_Project_Path_Result result;
#ifndef _WIN32
    char *normalized_asset = strdup(asset_path);
    if (normalized_asset == NULL) return MUSI_PROJECT_PATH_ERROR_TOO_LONG;
    for (char *at = normalized_asset + 1; *at != '\0'; ++at) {
        if (*at == '\\') *at = '/';
    }
    relative_asset = normalized_asset;
#endif
    size_t asset_length = strlen(relative_asset);
    bool directory_has_separator = directory_length > 0 &&
        (directory[directory_length - 1] == '/' ||
         directory[directory_length - 1] == '\\');
    size_t join_separator = directory_has_separator ? 0u : 1u;
    if (directory_length > SIZE_MAX - join_separator ||
        directory_length + join_separator > SIZE_MAX - asset_length - 1u) {
        result = MUSI_PROJECT_PATH_ERROR_TOO_LONG;
        goto cleanup_normalized;
    }
    size_t candidate_capacity = directory_length + join_separator +
                                asset_length + 1u;
    char *candidate = malloc(candidate_capacity);
    if (candidate == NULL) {
        result = MUSI_PROJECT_PATH_ERROR_TOO_LONG;
        goto cleanup_normalized;
    }
    memcpy(candidate, directory, directory_length);
    size_t at = directory_length;
    if (join_separator != 0) candidate[at++] = separator_character;
    memcpy(candidate + at, relative_asset, asset_length + 1u);

    if (project_regular_file_exists(candidate)) {
        result = project_copy_resolved_path(
            candidate, MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE,
            resolved, capacity);
    } else if (project_regular_file_exists(relative_asset)) {
        result = project_copy_resolved_path(
            relative_asset, MUSI_PROJECT_PATH_RESOLVED_LEGACY_CWD,
            resolved, capacity);
    } else {
        result = MUSI_PROJECT_PATH_ERROR_NOT_FOUND;
    }
    free(candidate);
cleanup_normalized:
#ifndef _WIN32
    free(normalized_asset);
#endif
    return result;
}

static char *project_canonicalize_existing_file_alloc(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = project_utf8_to_wide(path);
    if (wide == NULL) return NULL;
    DWORD required = GetFullPathNameW(wide, 0, NULL, NULL);
    if (required == 0 || required > SIZE_MAX/sizeof(wchar_t)) {
        free(wide);
        return NULL;
    }
    wchar_t *full = malloc((size_t)required*sizeof(*full));
    if (full == NULL) {
        free(wide);
        return NULL;
    }
    DWORD written = GetFullPathNameW(wide, required, full, NULL);
    free(wide);
    if (written == 0 || written >= required) {
        free(full);
        return NULL;
    }
    char *utf8 = project_wide_to_utf8(full);
    free(full);
    if (utf8 == NULL) return NULL;
    if (!project_regular_file_exists(utf8)) {
        free(utf8);
        return NULL;
    }
    return utf8;
#else
    char *canonical = realpath(path, NULL);
    if (canonical != NULL && !project_regular_file_exists(canonical)) {
        free(canonical);
        canonical = NULL;
    }
    return canonical;
#endif
}

Musi_Project_Path_Result musi_project_canonicalize_existing_file(
    const char *path, char *resolved, size_t capacity)
{
    if (path == NULL || path[0] == '\0' || resolved == NULL || capacity == 0) {
        return MUSI_PROJECT_PATH_ERROR_NULL;
    }
    char *canonical = project_canonicalize_existing_file_alloc(path);
    if (canonical == NULL) return MUSI_PROJECT_PATH_ERROR_NOT_FOUND;
    Musi_Project_Path_Result result = project_copy_resolved_path(
        canonical, MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE, resolved, capacity);
    free(canonical);
    return result;
}

static char *project_directory_copy(const char *project_path)
{
    size_t project_length = strlen(project_path);
    if (project_length == 0 ||
        project_path_character_is_separator(project_path[project_length - 1])) {
        return NULL;
    }
    const char *separator = project_last_separator(project_path);
    const char *directory = ".";
    size_t directory_length = 1;
    if (separator != NULL) {
        directory = project_path;
        directory_length = (size_t)(separator - project_path);
        if (directory_length == 0 ||
            (directory_length == 2 && project_path[1] == ':')) {
            ++directory_length;
        }
    }
    if (directory_length == SIZE_MAX) return NULL;
    char *copy = malloc(directory_length + 1u);
    if (copy == NULL) return NULL;
    memcpy(copy, directory, directory_length);
    copy[directory_length] = '\0';
    return copy;
}

#ifdef _WIN32
static char *project_canonicalize_existing_directory_alloc(const char *path)
{
    wchar_t *wide = project_utf8_to_wide(path);
    if (wide == NULL) return NULL;
    DWORD required = GetFullPathNameW(wide, 0, NULL, NULL);
    if (required == 0 || required > SIZE_MAX/sizeof(wchar_t)) {
        free(wide);
        return NULL;
    }
    wchar_t *full = malloc((size_t)required*sizeof(*full));
    if (full == NULL) {
        free(wide);
        return NULL;
    }
    DWORD written = GetFullPathNameW(wide, required, full, NULL);
    free(wide);
    if (written == 0 || written >= required) {
        free(full);
        return NULL;
    }
    char *utf8 = project_wide_to_utf8(full);
    free(full);
    if (utf8 == NULL) return NULL;
    wchar_t *canonical_wide = project_utf8_to_wide(utf8);
    if (canonical_wide == NULL) {
        free(utf8);
        return NULL;
    }
    DWORD attributes = GetFileAttributesW(canonical_wide);
    free(canonical_wide);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static bool project_path_prefix_character_equal(char first, char second)
{
    if (project_path_character_is_separator(first) &&
        project_path_character_is_separator(second)) return true;
    if (first >= 'A' && first <= 'Z') first = (char)(first - 'A' + 'a');
    if (second >= 'A' && second <= 'Z') second = (char)(second - 'A' + 'a');
    return first == second;
}
#else
static char *project_canonicalize_existing_directory_alloc(const char *path)
{
    char *canonical = realpath(path, NULL);
    if (canonical == NULL) return NULL;
    struct stat status;
    if (stat(canonical, &status) != 0 || !S_ISDIR(status.st_mode)) {
        free(canonical);
        return NULL;
    }
    return canonical;
}

static bool project_path_prefix_character_equal(char first, char second)
{
    return first == second;
}
#endif

static bool project_relative_path_is_unambiguous(const char *path)
{
    if (path == NULL || path[0] == '\0' || project_path_is_absolute(path)) {
        return false;
    }
    const char *component = path;
    for (const char *at = path; ; ++at) {
#ifndef _WIN32
        // The resolver treats backslashes in stored relative paths as portable
        // separators. A literal POSIX filename containing one must stay absolute.
        if (*at == '\\') return false;
#endif
        if (*at == '/' || *at == '\\' || *at == '\0') {
            size_t length = (size_t)(at - component);
            if (length == 0 || (length == 1 && component[0] == '.') ||
                (length == 2 && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            if (*at == '\0') return true;
            component = at + 1;
        }
    }
}

static char *project_relative_descendant_path(
    const char *directory, const char *asset)
{
    size_t directory_length = strlen(directory);
    size_t asset_length = strlen(asset);
    while (directory_length > 1u &&
           project_path_character_is_separator(directory[directory_length - 1])) {
#ifdef _WIN32
        if (directory_length == 3u && directory[1] == ':') break;
#endif
        --directory_length;
    }
    if (asset_length <= directory_length) return NULL;
    for (size_t index = 0; index < directory_length; ++index) {
        if (!project_path_prefix_character_equal(directory[index], asset[index])) {
            return NULL;
        }
    }
    size_t relative_offset = directory_length;
    if (!project_path_character_is_separator(directory[directory_length - 1])) {
        if (!project_path_character_is_separator(asset[relative_offset])) return NULL;
        ++relative_offset;
    }
    if (relative_offset >= asset_length) return NULL;
    size_t relative_length = asset_length - relative_offset;
    char *relative = malloc(relative_length + 1u);
    if (relative == NULL) return NULL;
    memcpy(relative, asset + relative_offset, relative_length + 1u);
#ifdef _WIN32
    for (char *at = relative; *at != '\0'; ++at) {
        if (*at == '\\') *at = '/';
    }
#endif
    if (!project_relative_path_is_unambiguous(relative)) {
        free(relative);
        return NULL;
    }
    return relative;
}

static bool project_relative_path_round_trips(
    const char *project_path, const char *relative_path,
    const char *canonical_asset)
{
    size_t project_length = strlen(project_path);
    size_t relative_length = strlen(relative_path);
    if (relative_length > SIZE_MAX - 4u ||
        project_length > SIZE_MAX - relative_length - 4u) return false;
    size_t capacity = project_length + relative_length + 4u;
    char *resolved = malloc(capacity);
    if (resolved == NULL) return false;
    Musi_Project_Path_Result result = musi_project_resolve_asset_path(
        project_path, relative_path, resolved, capacity);
    bool matches = result == MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE &&
                   musi_project_existing_files_alias(resolved, canonical_asset);
    free(resolved);
    return matches;
}

static Musi_Project_Stored_Path_Result project_copy_stored_path(
    const char *source, Musi_Project_Stored_Path_Result success,
    char *stored, size_t capacity)
{
    size_t length = strlen(source);
    if (length >= capacity) return MUSI_PROJECT_STORED_PATH_ERROR_TOO_LONG;
    memcpy(stored, source, length + 1u);
    return success;
}

Musi_Project_Stored_Path_Result musi_project_asset_path_for_storage(
    const char *project_path, const char *canonical_asset_path,
    char *stored, size_t capacity)
{
    if (project_path == NULL || project_path[0] == '\0' ||
        canonical_asset_path == NULL || canonical_asset_path[0] == '\0' ||
        stored == NULL || capacity == 0) {
        return MUSI_PROJECT_STORED_PATH_ERROR_NULL;
    }
    if (!project_path_is_absolute(canonical_asset_path)) {
        return MUSI_PROJECT_STORED_PATH_ERROR_INVALID;
    }
    char *canonical_asset =
        project_canonicalize_existing_file_alloc(canonical_asset_path);
    if (canonical_asset == NULL) return MUSI_PROJECT_STORED_PATH_ERROR_INVALID;

    char *directory = project_directory_copy(project_path);
    char *canonical_directory = directory == NULL ? NULL :
        project_canonicalize_existing_directory_alloc(directory);
    char *relative = canonical_directory == NULL ? NULL :
        project_relative_descendant_path(canonical_directory, canonical_asset);
    bool use_relative = relative != NULL && project_relative_path_round_trips(
        project_path, relative, canonical_asset);
    Musi_Project_Stored_Path_Result result = project_copy_stored_path(
        use_relative ? relative : canonical_asset,
        use_relative ? MUSI_PROJECT_STORED_PATH_RELATIVE :
                       MUSI_PROJECT_STORED_PATH_ABSOLUTE,
        stored, capacity);
    free(relative);
    free(canonical_directory);
    free(directory);
    free(canonical_asset);
    return result;
}

Musi_Project_Path_Result musi_project_resolve_bundled_asset_path(
    const char *project_path, const char *asset_path,
    char *resolved, size_t capacity)
{
    if (project_path == NULL || project_path[0] == '\0' ||
        asset_path == NULL || resolved == NULL || capacity == 0) {
        return MUSI_PROJECT_PATH_ERROR_NULL;
    }
    if (!project_relative_path_is_unambiguous(asset_path)) {
        return MUSI_PROJECT_PATH_ERROR_NOT_FOUND;
    }
    size_t temporary_capacity = strlen(project_path) + strlen(asset_path) + 4u;
    char *temporary = malloc(temporary_capacity);
    if (temporary == NULL) return MUSI_PROJECT_PATH_ERROR_TOO_LONG;
    Musi_Project_Path_Result result = musi_project_resolve_asset_path(
        project_path, asset_path, temporary, temporary_capacity);
    if (result != MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE) {
        free(temporary);
        return result == MUSI_PROJECT_PATH_ERROR_TOO_LONG ? result :
               MUSI_PROJECT_PATH_ERROR_NOT_FOUND;
    }
    char *canonical_asset = project_canonicalize_existing_file_alloc(temporary);
    char *directory = project_directory_copy(project_path);
    char *canonical_directory = directory == NULL ? NULL :
        project_canonicalize_existing_directory_alloc(directory);
    char *relative = canonical_asset == NULL || canonical_directory == NULL ? NULL :
        project_relative_descendant_path(canonical_directory, canonical_asset);
    if (relative == NULL || strcmp(relative, asset_path) != 0) {
        result = MUSI_PROJECT_PATH_ERROR_NOT_FOUND;
    } else {
        result = project_copy_resolved_path(
            canonical_asset, MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE,
            resolved, capacity);
    }
    free(relative);
    free(canonical_directory);
    free(directory);
    free(canonical_asset);
    free(temporary);
    return result;
}

const char *musi_project_stored_path_result_string(
    Musi_Project_Stored_Path_Result result)
{
    static const char *names[] = {
        "project-relative asset path",
        "absolute asset path",
        "null or empty path argument",
        "invalid or missing canonical asset path",
        "stored asset path is too long",
    };
    return result >= 0 && (size_t)result < sizeof(names)/sizeof(names[0])
               ? names[result] : "unknown stored project path result";
}

bool musi_project_existing_files_alias(const char *first, const char *second)
{
    if (first == NULL || second == NULL || first[0] == '\0' || second[0] == '\0') {
        return false;
    }
#ifdef _WIN32
    wchar_t *wide_first = project_utf8_to_wide(first);
    wchar_t *wide_second = project_utf8_to_wide(second);
    if (wide_first == NULL || wide_second == NULL) {
        free(wide_first);
        free(wide_second);
        return false;
    }
    HANDLE first_handle = CreateFileW(
        wide_first, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE second_handle = CreateFileW(
        wide_second, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide_first);
    free(wide_second);
    if (first_handle == INVALID_HANDLE_VALUE || second_handle == INVALID_HANDLE_VALUE) {
        if (first_handle != INVALID_HANDLE_VALUE) CloseHandle(first_handle);
        if (second_handle != INVALID_HANDLE_VALUE) CloseHandle(second_handle);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION first_info;
    BY_HANDLE_FILE_INFORMATION second_info;
    bool ok = GetFileInformationByHandle(first_handle, &first_info) &&
              GetFileInformationByHandle(second_handle, &second_info);
    CloseHandle(first_handle);
    CloseHandle(second_handle);
    return ok && first_info.dwVolumeSerialNumber == second_info.dwVolumeSerialNumber &&
           first_info.nFileIndexHigh == second_info.nFileIndexHigh &&
           first_info.nFileIndexLow == second_info.nFileIndexLow;
#else
    struct stat first_status;
    struct stat second_status;
    return stat(first, &first_status) == 0 && S_ISREG(first_status.st_mode) &&
           stat(second, &second_status) == 0 && S_ISREG(second_status.st_mode) &&
           first_status.st_dev == second_status.st_dev &&
           first_status.st_ino == second_status.st_ino;
#endif
}

uint64_t musi_project_process_id(void)
{
#ifdef _WIN32
    return (uint64_t)GetCurrentProcessId();
#else
    return (uint64_t)getpid();
#endif
}

bool musi_project_path_result_is_success(Musi_Project_Path_Result result)
{
    return result == MUSI_PROJECT_PATH_RESOLVED_ABSOLUTE ||
           result == MUSI_PROJECT_PATH_RESOLVED_PROJECT_RELATIVE ||
           result == MUSI_PROJECT_PATH_RESOLVED_LEGACY_CWD;
}

const char *musi_project_path_result_string(Musi_Project_Path_Result result)
{
    static const char *names[] = {
        "absolute asset resolved",
        "project-relative asset resolved",
        "legacy working-directory asset resolved",
        "null or empty path argument",
        "asset not found",
        "resolved path is too long",
    };
    return result >= 0 && (size_t)result < sizeof(names)/sizeof(names[0])
               ? names[result] : "unknown project path result";
}

bool musi_project_temporary_path(const char *destination,
                                 uint64_t process_id, uint64_t nonce,
                                 char *temporary, size_t capacity)
{
    if (destination == NULL || destination[0] == '\0' ||
        temporary == NULL || capacity == 0) return false;
    size_t destination_length = strlen(destination);
    if (destination[destination_length - 1] == '/' ||
        destination[destination_length - 1] == '\\') return false;

    const char *separator = project_last_separator(destination);
    size_t directory_length = separator == NULL ? 0u :
                              (size_t)(separator - destination) + 1u;
    int length = snprintf(
        temporary, capacity,
        "%.*s.musializer-project-%llu-%016llx.tmp",
        (int)directory_length, destination,
        (unsigned long long)process_id,
        (unsigned long long)nonce);
    return length > 0 && (size_t)length < capacity;
}

static uint64_t project_next_transaction_nonce(void)
{
#ifdef _WIN32
    static volatile LONG64 counter;
    return (uint64_t)InterlockedIncrement64(&counter);
#else
    static uint64_t counter;
    return __atomic_add_fetch(&counter, UINT64_C(1), __ATOMIC_RELAXED);
#endif
}

#ifndef _WIN32
static bool project_sync_parent_directory(const char *destination)
{
    const char *separator = project_last_separator(destination);
    size_t length = separator == NULL ? 1u :
                    (size_t)(separator - destination);
    const char *source = destination;
    if (separator == NULL) {
        source = ".";
    } else if (length == 0) {
        length = 1;
    }
    char *directory = malloc(length + 1u);
    if (directory == NULL) return false;
    memcpy(directory, source, length);
    directory[length] = '\0';

    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int directory_fd;
    do {
        directory_fd = open(directory, flags);
    } while (directory_fd < 0 && errno == EINTR);
    bool ok = directory_fd >= 0;
    if (directory_fd >= 0) {
        while (fsync(directory_fd) != 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        if (close(directory_fd) != 0) ok = false;
    }
    free(directory);
    return ok;
}
#endif

Musi_Project_File_Result musi_project_atomic_write(
    const char *destination, const void *data, size_t size)
{
    if (destination == NULL || destination[0] == '\0' ||
        (data == NULL && size != 0)) return MUSI_PROJECT_FILE_ERROR_NULL;
    size_t destination_length = strlen(destination);
    if (destination[destination_length - 1] == '/' ||
        destination[destination_length - 1] == '\\' ||
        destination_length > SIZE_MAX - 96u) {
        return MUSI_PROJECT_FILE_ERROR_PATH;
    }
    size_t temporary_capacity = destination_length + 96u;
    char *temporary = malloc(temporary_capacity);
    if (temporary == NULL) return MUSI_PROJECT_FILE_ERROR_PATH;

#ifdef _WIN32
    wchar_t *wide_destination = project_utf8_to_wide(destination);
    if (wide_destination == NULL) {
        free(temporary);
        return MUSI_PROJECT_FILE_ERROR_PATH;
    }
    HANDLE file = INVALID_HANDLE_VALUE;
    wchar_t *wide_temporary = NULL;
    for (unsigned attempt = 0; attempt < 256u; ++attempt) {
        uint64_t nonce = project_next_transaction_nonce();
        if (!musi_project_temporary_path(destination,
                (uint64_t)GetCurrentProcessId(), nonce,
                temporary, temporary_capacity)) break;
        free(wide_temporary);
        wide_temporary = project_utf8_to_wide(temporary);
        if (wide_temporary == NULL) break;
        file = CreateFileW(wide_temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS &&
            GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    if (file == INVALID_HANDLE_VALUE) {
        free(wide_temporary);
        free(wide_destination);
        free(temporary);
        return MUSI_PROJECT_FILE_ERROR_OPEN;
    }

    Musi_Project_File_Result result = MUSI_PROJECT_FILE_OK;
    const unsigned char *cursor = data;
    size_t remaining = size;
    while (remaining > 0) {
        DWORD chunk = remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining;
        DWORD written = 0;
        if (!WriteFile(file, cursor, chunk, &written, NULL) || written == 0) {
            result = MUSI_PROJECT_FILE_ERROR_WRITE;
            break;
        }
        cursor += written;
        remaining -= written;
    }
    if (result == MUSI_PROJECT_FILE_OK && !FlushFileBuffers(file)) {
        result = MUSI_PROJECT_FILE_ERROR_SYNC;
    }
    if (!CloseHandle(file) && result == MUSI_PROJECT_FILE_OK) {
        result = MUSI_PROJECT_FILE_ERROR_CLOSE;
    }
    if (result == MUSI_PROJECT_FILE_OK &&
        !MoveFileExW(wide_temporary, wide_destination,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        result = MUSI_PROJECT_FILE_ERROR_PUBLISH;
    }
    if (result != MUSI_PROJECT_FILE_OK) (void)DeleteFileW(wide_temporary);
    free(wide_temporary);
    free(wide_destination);
#else
    int file = -1;
    mode_t mode = 0666;
    bool preserve_mode = false;
    struct stat existing;
    if (stat(destination, &existing) == 0 && S_ISREG(existing.st_mode)) {
        mode = existing.st_mode & 07777;
        preserve_mode = true;
    }
    for (unsigned attempt = 0; attempt < 256u; ++attempt) {
        uint64_t nonce = project_next_transaction_nonce();
        if (!musi_project_temporary_path(destination, (uint64_t)getpid(), nonce,
                                         temporary, temporary_capacity)) break;
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
        file = open(temporary, flags, mode);
        if (file >= 0) break;
        if (errno != EEXIST) break;
    }
    if (file < 0) {
        free(temporary);
        return MUSI_PROJECT_FILE_ERROR_OPEN;
    }

    Musi_Project_File_Result result = MUSI_PROJECT_FILE_OK;
    if (preserve_mode) {
        while (fchmod(file, mode) != 0) {
            if (errno == EINTR) continue;
            result = MUSI_PROJECT_FILE_ERROR_PERMISSIONS;
            break;
        }
    }
    const unsigned char *cursor = data;
    size_t remaining = size;
    while (result == MUSI_PROJECT_FILE_OK && remaining > 0) {
        size_t chunk = remaining;
#ifdef SSIZE_MAX
        if (chunk > (size_t)SSIZE_MAX) chunk = (size_t)SSIZE_MAX;
#endif
        ssize_t written = write(file, cursor, chunk);
        if (written < 0) {
            if (errno == EINTR) continue;
            result = MUSI_PROJECT_FILE_ERROR_WRITE;
            break;
        }
        if (written == 0) {
            result = MUSI_PROJECT_FILE_ERROR_WRITE;
            break;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    if (result == MUSI_PROJECT_FILE_OK) {
        while (fsync(file) != 0) {
            if (errno == EINTR) continue;
            result = MUSI_PROJECT_FILE_ERROR_SYNC;
            break;
        }
    }
    if (close(file) != 0 && result == MUSI_PROJECT_FILE_OK) {
        result = MUSI_PROJECT_FILE_ERROR_CLOSE;
    }
    if (result == MUSI_PROJECT_FILE_OK &&
        rename(temporary, destination) != 0) {
        result = MUSI_PROJECT_FILE_ERROR_PUBLISH;
    }
    if (result == MUSI_PROJECT_FILE_OK &&
        !project_sync_parent_directory(destination)) {
        result = MUSI_PROJECT_FILE_ERROR_DURABILITY;
    }
    if (result != MUSI_PROJECT_FILE_OK &&
        result != MUSI_PROJECT_FILE_ERROR_DURABILITY) {
        (void)unlink(temporary);
    }
#endif

    free(temporary);
    return result;
}

static bool project_sha256_text_valid(const char *value)
{
    if (value == NULL) return false;
    for (size_t index = 0; index < 64; ++index) {
        char character = value[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) return false;
    }
    return value[64] == '\0';
}

static bool project_ensure_directory(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = project_utf8_to_wide(path);
    if (wide == NULL) return false;
    bool ok = CreateDirectoryW(wide, NULL) != 0;
    if (!ok && GetLastError() == ERROR_ALREADY_EXISTS) {
        DWORD attributes = GetFileAttributesW(wide);
        ok = attributes != INVALID_FILE_ATTRIBUTES &&
             (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
             (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    }
    free(wide);
    return ok;
#else
    if (mkdir(path, 0777) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat status;
    return lstat(path, &status) == 0 && S_ISDIR(status.st_mode);
#endif
}

static size_t project_safe_extension(const char *path, char output[18])
{
    const char *name = project_last_separator(path);
    name = name == NULL ? path : name + 1;
    const char *dot = strrchr(name, '.');
    if (dot == NULL || dot == name) return 0;
    size_t length = strlen(dot);
    if (length < 2 || length >= 18) return 0;
    output[0] = '.';
    for (size_t index = 1; index < length; ++index) {
        char character = dot[index];
        if (character >= 'A' && character <= 'Z') {
            character = (char)(character - 'A' + 'a');
        }
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9'))) return 0;
        output[index] = character;
    }
    output[length] = '\0';
    return length;
}

static bool project_bundle_paths(
    const char *project_path, Musi_Project_Asset_Category category,
    const char *source_path, const char *sha256,
    char *stored_path, size_t stored_capacity,
    char *runtime_path, size_t runtime_capacity,
    char **root_directory, char **category_directory)
{
    const char *separator = project_last_separator(project_path);
    const char *filename = separator == NULL ? project_path : separator + 1;
    const char *dot = strrchr(filename, '.');
    if (dot == NULL || dot == filename) return false;
    size_t stem_length = (size_t)(dot - filename);
    if (stem_length > INT_MAX) return false;
    const char *category_name = category == MUSI_PROJECT_ASSET_AUDIO ?
                                "audio" : "images";
    char extension[18] = {0};
    (void)project_safe_extension(source_path, extension);
    int stored_length = snprintf(
        stored_path, stored_capacity, "%.*s.assets/%s/%s%s",
        (int)stem_length, filename, category_name, sha256, extension);
    if (stored_length <= 0 || (size_t)stored_length >= stored_capacity) return false;

    size_t directory_length = separator == NULL ? 1u :
                              (size_t)(separator - project_path);
    const char *directory = separator == NULL ? "." : project_path;
    if (separator != NULL && directory_length == 0) directory_length = 1;
    bool has_separator = directory_length > 0 &&
        project_path_character_is_separator(directory[directory_length - 1]);
    int runtime_length = snprintf(
        runtime_path, runtime_capacity, "%.*s%s%s",
        (int)directory_length, directory, has_separator ? "" : "/", stored_path);
    if (runtime_length <= 0 || (size_t)runtime_length >= runtime_capacity) return false;

    size_t root_length = directory_length + (has_separator ? 0u : 1u) +
                         stem_length + strlen(".assets");
    size_t category_length = root_length + 1u + strlen(category_name);
    char *root = malloc(root_length + 1u);
    char *child = malloc(category_length + 1u);
    if (root == NULL || child == NULL) {
        free(root);
        free(child);
        return false;
    }
    snprintf(root, root_length + 1u, "%.*s%s%.*s.assets",
             (int)directory_length, directory, has_separator ? "" : "/",
             (int)stem_length, filename);
    snprintf(child, category_length + 1u, "%s/%s", root, category_name);
    *root_directory = root;
    *category_directory = child;
    return true;
}

static bool project_hash_matches(const char *path, const char *expected)
{
    char actual[SHA256_HEX_SIZE];
    return sha256_file_hex(path, actual) && strcmp(actual, expected) == 0;
}

static Musi_Project_Bundle_Result project_copy_asset_transaction(
    const char *source, const char *destination, const char *expected_sha256)
{
    if (project_regular_file_exists(destination)) {
        return project_hash_matches(destination, expected_sha256) ?
               MUSI_PROJECT_BUNDLE_OK : MUSI_PROJECT_BUNDLE_ERROR_COLLISION;
    }
    size_t destination_length = strlen(destination);
    if (destination_length > SIZE_MAX - 96u) return MUSI_PROJECT_BUNDLE_ERROR_PATH;
    size_t temporary_capacity = destination_length + 96u;
    char *temporary = malloc(temporary_capacity);
    if (temporary == NULL) return MUSI_PROJECT_BUNDLE_ERROR_PATH;
    temporary[0] = '\0';
    Musi_Project_Bundle_Result result = MUSI_PROJECT_BUNDLE_ERROR_COPY;

#ifdef _WIN32
    wchar_t *wide_source = project_utf8_to_wide(source);
    wchar_t *wide_destination = project_utf8_to_wide(destination);
    wchar_t *wide_temporary = NULL;
    bool copied = false;
    if (wide_source == NULL || wide_destination == NULL) {
        result = MUSI_PROJECT_BUNDLE_ERROR_PATH;
    }
    for (unsigned attempt = 0;
         wide_source != NULL && wide_destination != NULL &&
         attempt < 256u && !copied;
         ++attempt) {
        if (!musi_project_temporary_path(
                destination, musi_project_process_id(),
                project_next_transaction_nonce(), temporary,
                temporary_capacity)) break;
        free(wide_temporary);
        wide_temporary = project_utf8_to_wide(temporary);
        if (wide_temporary == NULL) break;
        copied = CopyFileW(wide_source, wide_temporary, TRUE) != 0;
        if (!copied && GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (!copied && result != MUSI_PROJECT_BUNDLE_ERROR_PATH) {
        result = MUSI_PROJECT_BUNDLE_ERROR_COPY;
    } else if (copied) {
        HANDLE file = CreateFileW(wide_temporary, GENERIC_READ | GENERIC_WRITE,
                                  0, NULL, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                                  NULL);
        if (file == INVALID_HANDLE_VALUE || !FlushFileBuffers(file)) {
            result = MUSI_PROJECT_BUNDLE_ERROR_SYNC;
        } else if (!CloseHandle(file)) {
            file = INVALID_HANDLE_VALUE;
            result = MUSI_PROJECT_BUNDLE_ERROR_SYNC;
        } else if (!project_hash_matches(temporary, expected_sha256)) {
            file = INVALID_HANDLE_VALUE;
            result = MUSI_PROJECT_BUNDLE_ERROR_IDENTITY;
        } else if (MoveFileExW(wide_temporary, wide_destination,
                               MOVEFILE_WRITE_THROUGH)) {
            file = INVALID_HANDLE_VALUE;
            result = MUSI_PROJECT_BUNDLE_OK;
        } else {
            file = INVALID_HANDLE_VALUE;
            result = project_regular_file_exists(destination) ?
                     (project_hash_matches(destination, expected_sha256) ?
                      MUSI_PROJECT_BUNDLE_OK :
                      MUSI_PROJECT_BUNDLE_ERROR_COLLISION) :
                     MUSI_PROJECT_BUNDLE_ERROR_PUBLISH;
        }
        if (file != INVALID_HANDLE_VALUE) (void)CloseHandle(file);
    }
    if (wide_temporary != NULL) (void)DeleteFileW(wide_temporary);
    free(wide_temporary);
    free(wide_destination);
    free(wide_source);
#else
    int input = -1;
    int output = -1;
    do {
        input = open(source, O_RDONLY
#ifdef O_CLOEXEC
                     | O_CLOEXEC
#endif
        );
    } while (input < 0 && errno == EINTR);
    if (input < 0) {
        result = MUSI_PROJECT_BUNDLE_ERROR_SOURCE;
        goto bundle_posix_cleanup;
    }
    for (unsigned attempt = 0; attempt < 256u; ++attempt) {
        if (!musi_project_temporary_path(
                destination, musi_project_process_id(),
                project_next_transaction_nonce(), temporary,
                temporary_capacity)) break;
        output = open(temporary, O_WRONLY | O_CREAT | O_EXCL
#ifdef O_CLOEXEC
                      | O_CLOEXEC
#endif
                      , 0666);
        if (output >= 0 || errno != EEXIST) break;
    }
    if (output < 0) goto bundle_posix_cleanup;
    unsigned char buffer[65536];
    for (;;) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) goto bundle_posix_cleanup;
        if (count == 0) break;
        size_t offset = 0;
        while (offset < (size_t)count) {
            ssize_t written = write(output, buffer + offset,
                                    (size_t)count - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) goto bundle_posix_cleanup;
            offset += (size_t)written;
        }
    }
    while (fsync(output) != 0) {
        if (errno == EINTR) continue;
        result = MUSI_PROJECT_BUNDLE_ERROR_SYNC;
        goto bundle_posix_cleanup;
    }
    if (close(output) != 0) {
        output = -1;
        result = MUSI_PROJECT_BUNDLE_ERROR_SYNC;
        goto bundle_posix_cleanup;
    }
    output = -1;
    if (close(input) != 0) {
        input = -1;
        result = MUSI_PROJECT_BUNDLE_ERROR_SOURCE;
        goto bundle_posix_cleanup;
    }
    input = -1;
    if (!project_hash_matches(temporary, expected_sha256)) {
        result = MUSI_PROJECT_BUNDLE_ERROR_IDENTITY;
        goto bundle_posix_cleanup;
    }
    if (link(temporary, destination) != 0) {
        if (errno == EEXIST) {
            result = project_hash_matches(destination, expected_sha256) ?
                     MUSI_PROJECT_BUNDLE_OK :
                     MUSI_PROJECT_BUNDLE_ERROR_COLLISION;
        } else {
            result = MUSI_PROJECT_BUNDLE_ERROR_PUBLISH;
        }
        goto bundle_posix_cleanup;
    }
    if (!project_sync_parent_directory(destination)) {
        result = MUSI_PROJECT_BUNDLE_ERROR_SYNC;
        goto bundle_posix_cleanup;
    }
    result = MUSI_PROJECT_BUNDLE_OK;
bundle_posix_cleanup:
    if (output >= 0) (void)close(output);
    if (input >= 0) (void)close(input);
    if (temporary[0] != '\0') (void)unlink(temporary);
#endif
    free(temporary);
    return result;
}

Musi_Project_Bundle_Result musi_project_bundle_asset(
    const char *project_path, Musi_Project_Asset_Category category,
    const char *source_path, const char *expected_sha256,
    char *stored_path, size_t stored_capacity,
    char *runtime_path, size_t runtime_capacity)
{
    if (project_path == NULL || project_path[0] == '\0' ||
        source_path == NULL || source_path[0] == '\0' ||
        !project_sha256_text_valid(expected_sha256) || stored_path == NULL ||
        stored_capacity == 0 || runtime_path == NULL || runtime_capacity == 0 ||
        (category != MUSI_PROJECT_ASSET_AUDIO &&
         category != MUSI_PROJECT_ASSET_IMAGE)) {
        return MUSI_PROJECT_BUNDLE_ERROR_ARGUMENT;
    }
    if (!project_regular_file_exists(source_path) ||
        !project_hash_matches(source_path, expected_sha256)) {
        return MUSI_PROJECT_BUNDLE_ERROR_SOURCE;
    }
    char *root = NULL;
    char *child = NULL;
    if (!project_bundle_paths(
            project_path, category, source_path, expected_sha256,
            stored_path, stored_capacity, runtime_path, runtime_capacity,
            &root, &child)) {
        return MUSI_PROJECT_BUNDLE_ERROR_PATH;
    }
    Musi_Project_Bundle_Result result = MUSI_PROJECT_BUNDLE_OK;
    if (!project_ensure_directory(root) || !project_ensure_directory(child)) {
        result = MUSI_PROJECT_BUNDLE_ERROR_DIRECTORY;
    } else {
        result = project_copy_asset_transaction(
            source_path, runtime_path, expected_sha256);
    }
    free(child);
    free(root);
    return result;
}

Musi_Project_Bundle_Result musi_project_reference_published_asset(
    const char *project_path, Musi_Project_Asset_Category category,
    const char *source_path, const char *expected_sha256,
    char *stored_path, size_t stored_capacity,
    char *runtime_path, size_t runtime_capacity)
{
    if (project_path == NULL || project_path[0] == '\0' ||
        source_path == NULL || source_path[0] == '\0' ||
        !project_sha256_text_valid(expected_sha256) || stored_path == NULL ||
        stored_capacity == 0 || runtime_path == NULL || runtime_capacity == 0 ||
        (category != MUSI_PROJECT_ASSET_AUDIO &&
         category != MUSI_PROJECT_ASSET_IMAGE)) {
        return MUSI_PROJECT_BUNDLE_ERROR_ARGUMENT;
    }
    char *root = NULL;
    char *child = NULL;
    if (!project_bundle_paths(
            project_path, category, source_path, expected_sha256,
            stored_path, stored_capacity, runtime_path, runtime_capacity,
            &root, &child)) {
        return MUSI_PROJECT_BUNDLE_ERROR_PATH;
    }
    free(child);
    free(root);
    if (!project_regular_file_exists(source_path) ||
        !project_regular_file_exists(runtime_path) ||
        !musi_project_existing_files_alias(source_path, runtime_path)) {
        return MUSI_PROJECT_BUNDLE_ERROR_SOURCE;
    }
    return MUSI_PROJECT_BUNDLE_OK;
}

const char *musi_project_bundle_result_string(Musi_Project_Bundle_Result result)
{
    static const char *names[] = {
        "ok",
        "invalid bundle argument",
        "asset bundle path is too long or malformed",
        "asset bundle directory could not be created",
        "source asset is missing or changed identity",
        "asset could not be copied completely",
        "asset copy could not be made durable",
        "copied asset did not match its expected SHA-256",
        "content-addressed destination contains different data",
        "asset could not be published",
    };
    return result >= 0 && (size_t)result < sizeof(names)/sizeof(names[0]) ?
           names[result] : "unknown project bundle result";
}

const char *musi_project_file_result_string(Musi_Project_File_Result result)
{
    static const char *names[] = {
        "ok",
        "null or empty argument",
        "invalid or oversized destination path",
        "could not create a transaction file",
        "could not write the complete project",
        "could not preserve project permissions",
        "could not flush the project to storage",
        "could not close the project transaction",
        "could not atomically publish the project",
        "project was published but parent-directory durability was not confirmed",
    };
    return result >= 0 && (size_t)result < sizeof(names)/sizeof(names[0])
               ? names[result] : "unknown project file result";
}

/* Test-container policy overlay: retain diagnostics in kernel/init domains.
 * Other domains stay enforcing. Never modifies the recovery image itself. */
#include <stdio.h>
#include <string.h>
#include <sepol/policydb/policydb.h>
#include <sepol/policydb/hashtab.h>
#include <sepol/policydb/ebitmap.h>
int main(int argc,char **argv) {
    if(argc!=3 && (argc!=4 || strcmp(argv[3],"--all-types")))return 2;
    policydb_t p;policy_file_t f;policydb_init(&p);policy_file_init(&f);
    f.type=PF_USE_STDIO;f.fp=fopen(argv[1],"rb");if(!f.fp)return 3;
    if(policydb_read(&p,&f,0))return 4;
    fclose(f.fp);
    /* Android 17 genfscon paths use wildcard matching. Linux 6.3 only
     * implements prefix matching. A literal path followed by one final '*'
     * is exactly representable by removing that star. Leave patterns with
     * interior wildcards untouched, rather than broadening their labels. */
    unsigned converted=0, complex=0;
    for(genfs_t *g=p.genfs;g;g=g->next) {
        for(ocontext_t *o=g->head;o;o=o->next) {
            char *star=strchr(o->u.name,'*');
            if(!star)continue;
            if(star[1]){complex++;continue;}
            *star='\0';converted++;
        }
        for(ocontext_t *o=g->head;o;o=o->next)
            for(ocontext_t *n=o->next;n;n=n->next)
                if(!strcmp(o->u.name,n->u.name) && o->v.sclass==n->v.sclass &&
                   o->context[0].type!=n->context[0].type) {
                    fprintf(stderr,"Conflicting genfs labels: %s %s\n",g->fstype,o->u.name);
                    return 9;
                }
    }
    printf("GENFS_COMPAT: %u trailing-star paths converted; %u complex patterns unchanged\n",converted,complex);
    const char *domains[]={"kernel","init"};
    for(unsigned i=0;i<2;i++) {
        type_datum_t *t=hashtab_search(p.p_types.table,domains[i]);if(!t)return 5;
        /* Initial Linux /init and its diagnostic children map to init, while
         * kernel threads remain kernel. Android service domains are unchanged. */
        if(ebitmap_set_bit(&p.permissive_map,t->s.value,1))return 6;
        printf("TEST_POLICY: permissive %s domain (type %u)\n",domains[i],t->s.value);
    }
    if(argc==4) {
        /* Early porting only: the new vendor policy expects a newer kernel.
         * This RAM overlay is never a production security configuration. */
        for(unsigned i=1;i<=p.p_types.nprim;i++)
            if(ebitmap_set_bit(&p.permissive_map,i,1))return 6;
        printf("DEVELOPMENT_ONLY: all %u policy types permissive\n",p.p_types.nprim);
    }
    f.fp=fopen(argv[2],"wb");if(!f.fp)return 7;
    if(policydb_write(&p,&f))return 8;
    fclose(f.fp);
    policydb_destroy(&p);return 0;
}

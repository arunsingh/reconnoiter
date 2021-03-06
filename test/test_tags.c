#include <stdio.h>
#include "noit_metric.h"
#include "noit_metric_tag_search.h"
#include "noit_message_decoder.h"
#include <mtev_hash.h>
#include <mtev_b64.h>
#include <assert.h>
#include <sys/time.h>

const char *tcpairs[][2] = {

  { "/transmissions`latency|ST[b\"bjo6Og==\":b\"YT1i\",customer:noone,node:j.mta2vrest.cc.aws-usw2a.prd.acme,cluster:mta2]",
    "/transmissions`latency|ST[cluster:mta2,customer:noone,b\"bjo6Og==\":a=b,node:j.mta2vrest.cc.aws-usw2a.prd.acme]" },

  { "woop|ST[a:b,c:d]|MT{foo:bar}|ST[c:d,e:f,a:b]",
    "woop|ST[a:b,c:d,e:f]|MT{foo:bar}" },

  { "simple string with spaces",
    "simple string with spaces" },

  { "trailing space|ST[foo:bar] ", NULL },
  { "colonfest|ST[f:o:ba::::r:]", "colonfest|ST[f:o:ba::::r:]" },

  { "  a\n\t stupid\bbad person did this \t\r\n|ST[foo:bar]",
    "a   stupid bad person did this|ST[foo:bar]" },

  { "blank_values|ST[a:b,c:]|MT{foo:bar}|ST[e:f,a:b]",
    "blank_values|ST[a:b,c:,e:f]|MT{foo:bar}" },

  /* long val */
  { "metric|ST[short:tag,superlongtagthatismorethan256characterstotal:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,another:acceptabletag]",
    "metric|ST[another:acceptabletag,short:tag,superlongtagthatismorethan256characterstotal:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx_tldr_8e2bd46fa751af2d2b093c2f5465f786cbe2b8a9]" },

  /* long cat and long val */
  { "metric|ST[short:tag,superlongtagcatthatismorethan256characterstotalyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,another:acceptabletag]",
    "metric|ST[another:acceptabletag,short:tag,superlongtagcatthatismorethan256characterstotalyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy_tldr_2584f4690f561d00c607ad5333d7c31a3f06664c:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx_tldr_8e2bd46fa751af2d2b093c2f5465f786cbe2b8a9]" },

  /* long (but not too long) cat */
  { "metric|ST[short:tag,superlongtagcatthatismorethan256characterstotalyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,another:acceptabletag]",
    "metric|ST[another:acceptabletag,short:tag,superlongtagcatthatismorethan256characterstotalyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy_tldr_ce6340dfd8ee2343280ad9723eb4d54d87ebc435:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx]" },

};

const struct {
  const char *name;
  const char *input;
  const char *output;
  int rval;
  mtev_boolean allocd;
} testlines[] = {
  {
    "test1",
    "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\tSuperSimpleMetricName|ST[a:b,c:d]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==",
    "SuperSimpleMetricName|ST[a:b,c:d]",
    1, mtev_false
  },
  {
    "test2 (unordered and overly encoded)",
    "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\t/transmissions`latency|ST[b\"bjo6Og==\":b\"YT1i\",customer:noone,node:j.mta2vrest.cc.aws-usw2a.prd.acme,cluster:mta2]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==",
    "/transmissions`latency|ST[cluster:mta2,customer:noone,b\"bjo6Og==\":a=b,node:j.mta2vrest.cc.aws-usw2a.prd.acme]",
    1, mtev_true
  },
  {
    "test3 (invalid tag)",
    "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\t/transmissions`latency|ST[b\"bjo6Og==\":b\"YT1i\",c{ustomer:noone,node:j.mta2vrest.cc.aws-usw2a.prd.acme,cluster:mta2]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==",
    NULL,
    -7, mtev_false
  },
  {
    "test4 (spacefest)",
    "H1\t1525385460.000\tpush`httptrap`c_933_247631::httptrap`43e5c324-44c2-4877-a625-3b4c8230f2eb\t \bSuperSpacey\r\n|ST[a:b,c:d]\tAAUK/wACDP8AARH/AAEa/wABVP8AAQ==",
    "SuperSpacey|ST[a:b,c:d]",
    1, mtev_true
  },
  {
    "test5",
    "M\t127.0.0.1\t1526493506.214\tunknown`fault`c_1_77`4766c496-2173-4f60-9607-6449d29cac56\tfoo|ST[color:orange]\ti\t100\n",
    "foo|ST[color:orange]",
    1, mtev_false
   },
};

int ntest = 0;
int failures = 0;
#define test_assert(a) do { \
  ntest++; \
  if(a) { \
    printf("ok - %d (%s)\n", ntest, #a); \
  } else { \
    printf("not ok - %d (%s)\n", ntest, #a); \
    failures++; \
  } \
} while(0)

#define test_assert_namef(valid, fmt, args...) do { \
  int __valid = valid; \
  ntest++; \
  if(!__valid) failures++; \
  printf("%s - %d (", __valid ? "ok" : "not ok", ntest); \
  printf(fmt, args); \
  printf(")\n"); \
} while(0)

void test_tag_decode()
{
  char decoded[512] = {0};
  
  /* echo -n "foo:bar[stuff]" | base64 */
  const char *encoded = "b\"Zm9vOmJhcltzdHVmZl0=\":value";
  int rval  = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
					    encoded, strlen(encoded));
  test_assert(rval > 0);

  int eq = strncmp("foo:bar[stuff]\037value", decoded, rval);
  test_assert(eq == 0);

  encoded = "b\"Zm9vOmJhcltzdHVmZl0=\":b\"Zm9vOmJhcltzdHVmZl0=\"";
  rval = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
				       encoded, strlen(encoded));
  test_assert(rval > 0);

  eq = strncmp("foo:bar[stuff]\037foo:bar[stuff]", decoded, rval);
  test_assert(eq == 0);

  encoded = "category:b\"Zm9vOmJhcltzdHVmZl0=\"";
  rval = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
				       encoded, strlen(encoded));
  test_assert(rval > 0);

  eq = strncmp("category\037foo:bar[stuff]", decoded, rval);
  test_assert(eq == 0);

  /* normal tags should just pass through with copies */
  encoded = "category:value";
  rval = noit_metric_tagset_decode_tag(decoded, sizeof(decoded),
				       encoded, strlen(encoded));
  test_assert(rval > 0);

  eq = strncmp("category\037value", decoded, rval);
  test_assert(eq == 0);

}

void test_ast_decode()
{
  int erroroffset;
  noit_metric_tag_search_ast_t *ast;

  /* simple test */
  ast = noit_metric_tag_search_parse("and(foo:bar)", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  noit_metric_tag_search_free(ast);

  /* simple test with colons in value */
  ast = noit_metric_tag_search_parse("and(foo:bar:baz)", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar:baz") == 0);
  noit_metric_tag_search_free(ast);

  
  /* half / test */
  ast = noit_metric_tag_search_parse("and(/endpoint`latency:/bar)", &erroroffset);
  if(ast) {
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"/endpoint`latency") == 0);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"/bar") == 0);
  noit_metric_tag_search_free(ast);
  }
  else test_assert_namef(ast != NULL, "error at %d", erroroffset);
  
  /* simple re key test */
  ast = noit_metric_tag_search_parse("and(/foo/:bar)", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(ast->contents.args.node[0]->contents.spec.cat.re != NULL);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  noit_metric_tag_search_free(ast);
  
  /* simple re val test */
  ast = noit_metric_tag_search_parse("and(foo:/bar/)", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  test_assert(ast->contents.args.node[0]->contents.spec.name.re != NULL);
  noit_metric_tag_search_free(ast);
  
  /* base64 fixed category */
  ast = noit_metric_tag_search_parse("and(foo:bar,not(b\"c29tZTpzdHVmZltoZXJlXQ==\":value))", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  test_assert(ast->contents.args.node[1]->operation == OP_NOT_ARGS);
  noit_metric_tag_search_ast_t *not = ast->contents.args.node[1]->contents.args.node[0];
  test_assert(not != NULL);
  test_assert(strcmp(not->contents.spec.cat.str,"some:stuff[here]") == 0);
  test_assert(strcmp(not->contents.spec.name.str,"value") == 0);
  noit_metric_tag_search_free(ast);

  /* base64 regex parse */
  ast = noit_metric_tag_search_parse("and(foo:bar,not(b/c29tZS4q/:value))", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str,"foo") == 0);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  test_assert(ast->contents.args.node[1]->operation == OP_NOT_ARGS);
  not = ast->contents.args.node[1]->contents.args.node[0];
  test_assert(not != NULL);
  test_assert(strcmp(not->contents.spec.cat.str,"some.*") == 0);
  test_assert(not->contents.spec.cat.re != NULL);
  test_assert(strcmp(not->contents.spec.name.str,"value") == 0);
  noit_metric_tag_search_free(ast);

  /* wildcard category parse */
  ast = noit_metric_tag_search_parse("and(*:bar)", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(ast->contents.args.node[0]->contents.spec.cat.re != NULL);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  noit_metric_tag_search_free(ast);

  /* wildcard category parse */
  ast = noit_metric_tag_search_parse("and(f*:bar)", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(ast->contents.args.node[0]->contents.spec.cat.re != NULL);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.name.str,"bar") == 0);
  noit_metric_tag_search_free(ast);

  /* wildcard value parse */
  ast = noit_metric_tag_search_parse("and(foo:b*r)", &erroroffset);
  test_assert(ast != NULL);
  test_assert(ast->operation == OP_AND_ARGS);
  test_assert(ast->contents.args.node[0]->operation == OP_MATCH);
  test_assert(strcmp(ast->contents.args.node[0]->contents.spec.cat.str, "foo") == 0);
  test_assert(ast->contents.args.node[0]->contents.spec.name.re != NULL);
  noit_metric_tag_search_free(ast);

}

void test_tag_match()
{
  int erroroffset;
  noit_metric_tagset_t tagset;
  noit_metric_tagset_builder_t builder;
  noit_metric_tagset_builder_start(&builder);

  const char *tagstring = "foo:bar,b\"c29tZTpzdHVmZltoZXJlXQ==\":value";
  noit_metric_tagset_builder_add_many(&builder, tagstring, strlen(tagstring));
  char *canonical;
  noit_metric_tagset_builder_end(&builder, &tagset, &canonical);
  
  /* simple test */
  noit_metric_tag_search_ast_t *ast = noit_metric_tag_search_parse("and(foo:bar)", &erroroffset);
  mtev_boolean match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
  test_assert(match == mtev_true);
  noit_metric_tag_search_free(ast);

  ast = noit_metric_tag_search_parse("and(foo:bar,b\"c29tZTpzdHVmZltoZXJlXQ==\":value)", &erroroffset);
  match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
  test_assert(match == mtev_true);
  noit_metric_tag_search_free(ast);

  ast = noit_metric_tag_search_parse("and(b/c29tZS4q/:value)", &erroroffset);
  match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
  test_assert(match == mtev_true);
  noit_metric_tag_search_free(ast);

  ast = noit_metric_tag_search_parse("and(quux:value)", &erroroffset);
  match = noit_metric_tag_search_evaluate_against_tags(ast, &tagset);
  test_assert(match == mtev_false);
  noit_metric_tag_search_free(ast);
}

void test_canon(const char *in, const char *expect) {
  int len;
  char buff[MAX_METRIC_TAGGED_NAME];
  len = noit_metric_canonicalize(in, strlen(in), buff, sizeof(buff), mtev_true);
  if(len < 0) test_assert_namef(expect == NULL, "canon(%s) -> NULL", in);
  else test_assert_namef(expect && !strcmp(expect, buff), "canon(%s) -> %s", expect, buff);
}

void test_fuzz_canon() {
  for(int encode = 0; encode < 4; encode++) {
    int cnt = 0;
    char buff[4096];
    char _obuff[32678];
    char *obuff = _obuff + 10;
    memcpy(_obuff, "metric|ST[", 10);
    char out[MAX_METRIC_TAGGED_NAME];
    mtev_hash_table unique;
    mtev_hash_init(&unique);
    for(int i=1; i<260; i++) {
      int offset = i;
  
      if(encode & 1) {
        memset(buff, ',', i);
        offset = mtev_b64_encode((unsigned char *)buff, i, obuff+2, sizeof(_obuff)-2);
        assert(offset > 0);
        obuff[0] = 'b'; obuff[1] = '"';
        obuff[offset + 2] = '"';
        offset += 3;
      } else {
        memset(buff, 'c', i);
        memcpy(obuff, buff, offset);
      }
      obuff[offset] = ':';
      for(int j=1; j<260; j++) {
        if(encode & 2) {
          memset(buff, ',', j);
          int vlen = mtev_b64_encode((unsigned char *)buff, j, obuff+offset+3, sizeof(_obuff)-(offset+4));
          assert(vlen > 0);
          obuff[offset+1] = 'b'; obuff[offset+2] = '"';
          obuff[offset + vlen + 3] = '"';
          obuff[offset + vlen + 4] = ']';
          obuff[offset + vlen + 5] = '\0';
        } else {
          memset(buff, 'v', j);
          memset(obuff+offset+1, 'v', j);
          obuff[offset+j+1] = ']';
          obuff[offset+j+2] = '\0';
        }
        /* We can only operate on metric names up to sizeof(out), so just elide the testing
         * from longer names. */
        if(strlen(_obuff) <= sizeof(out)) {
          int len = noit_metric_canonicalize(_obuff, strlen(_obuff), out, sizeof(out), mtev_true);
          if(len < 0) {
            test_assert_namef(len > 0, "canonicalization failed %d (%s)", len, _obuff);
            return;
          }
          mtev_hash_replace(&unique, strdup(out), strlen(out), NULL, free, NULL);
          cnt++;
        }
      }
    }
    test_assert_namef(mtev_hash_size(&unique) == cnt, "fuzzed uniquely [%c%c] (%d==%d)",
                      encode & 1 ? 'b' : '-',
                      encode & 2 ? 'b' : '-',
                      mtev_hash_size(&unique), cnt);
    mtev_hash_destroy(&unique, free, NULL);
  }
}

void test_line(const char *name, const char *in, const char *expect, int rval_expect, mtev_boolean allocd) {
  noit_metric_message_t message;
  memset(&message, 0, sizeof(message));
  message.original_message = (char *)in;
  message.original_message_len = strlen(in);
  int rval = noit_message_decoder_parse_line(&message, -1);
  test_assert_namef(allocd != (message.id.alloc_name == NULL), "test_line(%s) allocd", name);
  test_assert_namef(rval == rval_expect, "test_line(%s) rval [%d should be %d]", name, rval, rval_expect);
  if(expect != NULL) {
     int v = strlen(expect) == message.id.name_len_with_tags &&
             !memcmp(message.id.name, expect, message.id.name_len_with_tags);
     test_assert_namef(v,
                       "test_line(%s) metric match", name);
     if(v == 0) {
       printf("\nFAILURE:\nRESULT: '%.*s'\nEXPECT: '%s'\n\n",
              (int)message.id.name_len_with_tags, message.id.name, expect);
     }
  }
  else {
    test_assert_namef(message.id.name_len_with_tags == 0 || rval < 0,
                      "test_line(%s) metric null", name);
  }
  noit_metric_message_clear(&message);
}

void metric_parsing(void) {
  int len;
  char buff[NOIT_TAG_MAX_PAIR_LEN], dbuff[NOIT_TAG_MAX_PAIR_LEN], ebuff[NOIT_TAG_MAX_PAIR_LEN];

  snprintf(buff, sizeof(buff), "@#lkm45lsnd:kljnmsdkflnsdf:kjnsdkfjnsdf");
  len = noit_metric_tagset_decode_tag(dbuff, sizeof(dbuff), buff, strlen(buff));
  test_assert(len > 0);
  len = noit_metric_tagset_encode_tag(ebuff, sizeof(ebuff), dbuff, len);
  test_assert(len > 0);
  test_assert(strcmp(buff, ebuff) == 0);

  snprintf(buff, sizeof(buff), "b\"Zm9v\":http://12.3.3.4:80/this?is=it");
  len = noit_metric_tagset_decode_tag(dbuff, sizeof(dbuff), buff, strlen(buff));
  test_assert(len > 0);
  len = noit_metric_tagset_encode_tag(ebuff, sizeof(ebuff), dbuff, len);
  test_assert(len > 0);
  test_assert(strcmp("foo:http://12.3.3.4:80/this?is=it", ebuff) == 0);

  int i;

  for(i=0; i<sizeof(testlines)/sizeof(*testlines); i++) {
    test_line(testlines[i].name, testlines[i].input, testlines[i].output, testlines[i].rval, testlines[i].allocd);
  }

  for(int i=0; i<sizeof(tcpairs)/sizeof(*tcpairs); i++) {
    test_canon(tcpairs[i][0], tcpairs[i][1]);
  }
  test_fuzz_canon();
}

void query_parsing(void) {
  int erroroffset = 0;
  noit_metric_tag_search_ast_t *ast = noit_metric_tag_search_parse("and(*:*)", &erroroffset);
  test_assert(ast != NULL);
  char *unparse = noit_metric_tag_search_unparse(ast);
  printf("%s\n", unparse);
  free(unparse);
  noit_metric_tag_search_free(ast);

  ast = noit_metric_tag_search_parse("and(foo:bar)", &erroroffset);
  test_assert(ast != NULL);
  unparse = noit_metric_tag_search_unparse(ast);
  printf("%s\n", unparse);
  free(unparse);
  noit_metric_tag_search_free(ast);
  
  ast = noit_metric_tag_search_parse("and(or(foo:bar,c:d),e:f)", &erroroffset);
  test_assert(ast != NULL);
  unparse = noit_metric_tag_search_unparse(ast);
  printf("%s\n", unparse);
  free(unparse);
  noit_metric_tag_search_free(ast);

  ast = noit_metric_tag_search_parse("and(a:,foo:bar)", &erroroffset);
  test_assert(ast != NULL);
  unparse = noit_metric_tag_search_unparse(ast);
  printf("%s\n", unparse);
  free(unparse);
  noit_metric_tag_search_free(ast);

  ast = noit_metric_tag_search_parse("and(a:,foo:bar)", &erroroffset);
  test_assert(ast != NULL);
  unparse = noit_metric_tag_search_unparse(ast);
  printf("%s\n", unparse);
  free(unparse);
  noit_metric_tag_search_free(ast);

  ast = noit_metric_tag_search_parse("and(b\"ZmFydHM=\":b\"\",foo:bar)", &erroroffset);
  test_assert(ast != NULL);
  unparse = noit_metric_tag_search_unparse(ast);
  printf("%s\n", unparse);
  free(unparse);
  noit_metric_tag_search_free(ast);
  
}

void loop(char *str) {
  int len;
  const int nloop = 100000;
  struct timeval start, end;
  char *hbuff = strdup(str);
  int hlen = strlen(hbuff);
  char obuff[MAX_METRIC_TAGGED_NAME];
  gettimeofday(&start, NULL);
  for(int i=0; i<nloop; i++) {
    len = noit_metric_canonicalize(hbuff, hlen, obuff, sizeof(obuff), mtev_true);
    assert(len > 0);
  }
  gettimeofday(&end, NULL);
  double elapsed = sub_timeval_d(end, start);
  free(hbuff);
  printf("canonicalize('%s') -> %f ns/op\n", str, (elapsed * 1000000000.0) / (double)nloop);
}
int main(int argc, const char **argv)
{
  test_tag_decode();
  test_ast_decode();
  metric_parsing();
  query_parsing();
  printf("\nPerformance:\n====================\n");
  loop("woop|ST[a:b,c:d]|MT{foo:bar}|ST[c:d,e:f,a:b]");
  loop("testing_this|ST[cluster:mta2,customer:noone,b\"bjo6Og==\":a=b,node:j.mta2vrest.prd.acme]");
  loop("testing_this_long_untagged_metric");
  printf("\n%d tests failed.\n", failures);
  return !(failures == 0);
}

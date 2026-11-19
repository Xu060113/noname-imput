#import "RimeBridge.h"
#import "rime_api.h"

static RimeApi *engine;

@implementation PinyinSession {
    RimeSessionId session;
}
+ (BOOL)startWithData:(NSString *)shared user:(NSString *)user {
    if (engine) return YES;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:user withIntermediateDirectories:YES attributes:nil error:nil]) return NO;
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = shared.fileSystemRepresentation;
    traits.user_data_dir = user.fileSystemRepresentation;
    traits.distribution_name = "Yuju";
    traits.distribution_code_name = "yuju";
    traits.distribution_version = "1.0";
    traits.app_name = "rime.yuju";
    traits.log_dir = "";
    traits.min_log_level = 2;
    engine = rime_get_api();
    engine->setup(&traits);
    engine->initialize(&traits);
    return YES;
}
+ (instancetype)openEngineSession { return [[self alloc] init]; }
- (instancetype)init {
    if ((self = [super init])) {
        if (!engine || !(session = engine->create_session()) || !engine->select_schema(session, "yuju_pinyin")) return nil;
    }
    return self;
}
- (void)dealloc { if (session) engine->destroy_session(session); }
- (BOOL)press:(int)key { return engine->process_key(session, key, 0); }
- (void)clear { engine->clear_composition(session); }
- (void)finish { engine->commit_composition(session); }
- (NSString *)takeCommit {
    RIME_STRUCT(RimeCommit, commit);
    if (!engine->get_commit(session, &commit)) return @"";
    NSString *text = commit.text ? @(commit.text) : @"";
    engine->free_commit(&commit);
    return text;
}
- (NSString *)rawInput {
    const char *value = engine->get_input(session);
    return value ? @(value) : @"";
}
- (NSString *)marked {
    RIME_STRUCT(RimeContext, ctx);
    if (!engine->get_context(session, &ctx)) return @"";
    NSString *text = ctx.composition.preedit ? @(ctx.composition.preedit) : @"";
    engine->free_context(&ctx);
    return text;
}
- (NSUInteger)caret {
    RIME_STRUCT(RimeContext, ctx);
    if (!engine->get_context(session, &ctx)) return 0;
    NSString *prefix = ctx.composition.preedit ? [[NSString alloc] initWithBytes:ctx.composition.preedit length:ctx.composition.cursor_pos encoding:NSUTF8StringEncoding] : nil;
    engine->free_context(&ctx);
    return prefix.length;
}
- (NSInteger)highlighted {
    RIME_STRUCT(RimeContext, ctx);
    if (!engine->get_context(session, &ctx)) return 0;
    NSInteger index = ctx.menu.highlighted_candidate_index;
    engine->free_context(&ctx);
    return index;
}
- (NSArray<NSString *> *)choices {
    RIME_STRUCT(RimeContext, ctx);
    if (!engine->get_context(session, &ctx)) return @[];
    NSMutableArray *words = [NSMutableArray array];
    for (int i = 0; i < ctx.menu.num_candidates; i++) {
        [words addObject:@(ctx.menu.candidates[i].text)];
    }
    engine->free_context(&ctx);
    return words;
}
@end

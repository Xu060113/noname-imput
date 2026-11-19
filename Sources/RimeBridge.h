#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface PinyinSession : NSObject
+ (BOOL)startWithData:(NSString *)shared user:(NSString *)user;
+ (nullable instancetype)openEngineSession;
- (BOOL)press:(int)key;
- (void)clear;
- (void)finish;
@property(readonly) NSString *marked;
@property(readonly) NSString *rawInput;
@property(readonly) NSUInteger caret;
@property(readonly) NSInteger highlighted;
@property(readonly) NSArray<NSString *> *choices;
- (NSString *)takeCommit;
@end
NS_ASSUME_NONNULL_END

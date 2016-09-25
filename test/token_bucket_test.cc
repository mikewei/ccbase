#include <unistd.h>
#include "gtestx/gtestx.h"
#include "ccbase/token_bucket.h"

class TokenBucket : public testing::Test
{
protected:
  TokenBucket() : tb_(10000, 10000) {}
  virtual ~TokenBucket() {}
  virtual void SetUp() {
  }
  virtual void TearDown() {
  }
  ccb::TokenBucket tb_;
};

TEST_F(TokenBucket, Get)
{
  const unsigned int n = 10000;
  ASSERT_GE(tb_.tokens(), n);
  ASSERT_TRUE(tb_.Check(n));
  ASSERT_TRUE(tb_.Get(tb_.tokens()));
  ASSERT_EQ(0U, tb_.tokens());
  ASSERT_FALSE(tb_.Check(1));
  ASSERT_FALSE(tb_.Get(1));
  sleep(1);
  tb_.Gen();
  ASSERT_EQ(n, tb_.tokens());
  for (unsigned int i = 0; i < n; i++) {
    ASSERT_TRUE(tb_.Check(1));
    ASSERT_TRUE(tb_.Get(1));
  }
  ASSERT_FALSE(tb_.Check(1));
  ASSERT_FALSE(tb_.Get(1));
}

PERF_TEST_F(TokenBucket, Get_Perf)
{
  if (!tb_.Get(1)) tb_.Mod(10000, 10000);
}


App({
  globalData: {
    // ========== 华为云配置（请替换为你的实际信息）==========
    iamUser: "zjy",                            // IAM用户名
    iamPassword: "Graduation_Design",          // IAM用户密码
    domainName: "hid_k492a4n5bwnr7p1",         // 华为云主账号名
    region: "cn-south-1",                      // 区域（IAM用）
    projectId: "2a07bf0e4f0946d3984e0d8ec41e2f0c", // 项目ID
    deviceId: "69c8f536c00ccb6d4b47abb9_869701076094260", // 设备ID
    endpoint: "2966810d56.st1.iotda-app.cn-south-1.myhuaweicloud.com", // IoTDA实例接入地址（完整域名）
    
    // Token缓存
    iamToken: null,
    tokenExpireTime: 0
  },

  onLaunch() {
    // 尝试从缓存加载未过期的Token
    const cached = wx.getStorageSync('iam_token');
    if (cached && cached.token && cached.expireTime > Date.now()) {   // 判断Token是否存在、过期
      this.globalData.iamToken = cached.token;
      this.globalData.tokenExpireTime = cached.expireTime;
    }
  },

  // 获取有效Token（若即将过期则自动刷新）
  async getValidToken() {
    // 缓存中是否有Token，是否还有5分钟过期
    if (this.globalData.iamToken && this.globalData.tokenExpireTime > Date.now() + 5 * 60 * 1000) {
      return this.globalData.iamToken;
    }
    return await this.refreshToken();   // 刷新Token
  },

  // 刷新Token（使用IAM密码认证）
  refreshToken() {
    return new Promise((resolve, reject) => {
      const { iamUser, iamPassword, domainName, region } = this.globalData;
      wx.request({
        url: `https://iam.${region}.myhuaweicloud.com/v3/auth/tokens`,
        method: 'POST',
        data: {
          auth: {
            identity: {
              methods: ["password"],
              password: {
                user: {
                  name: iamUser,
                  password: iamPassword,
                  domain: { name: domainName }
                }
              }
            },
            scope: {
              project: { name: region }
            }
          }
        },
        success: (res) => {
          const token = res.header['X-Subject-Token'] || res.header['x-subject-token'];
          if (token) {
            // 假设Token有效期24小时，提前1小时刷新
            const expireTime = Date.now() + 23 * 60 * 60 * 1000;
            wx.setStorageSync('iam_token', { token, expireTime });
            this.globalData.iamToken = token;
            this.globalData.tokenExpireTime = expireTime;
            resolve(token);
          } else {
            reject(new Error('获取Token失败：响应头无Token'));
          }
        },
        fail: (err) => {
          console.error('Token请求失败', err);
          reject(err);
        }
      });
    });
  }
});
const app = getApp();
const coordtransform = require('coordtransform');

Page({
  data: {
    longitude: 116.397428,
    latitude: 39.90923,
    markers: [],
    deviceLat: null,
    deviceLng: null,
    heartRate: '--',
    spo2: '--',
    temp: '--',
    step: '--',
    alarmStatus: 'Normal',
    alarmText: '正常',
    alarmClass: 'normal',
    loading: false,
    lastUpdate: '',
    deviceOnline: true   // 真实在线状态
  },

  onLoad() {
    this.initStorage();
    this.fetchDeviceData();
    this.timer = setInterval(() => {
      this.fetchDeviceData();
    }, 30000);
  },

  initStorage() {
    if (!wx.getStorageSync('heart_rate_history')) wx.setStorageSync('heart_rate_history', []);
    if (!wx.getStorageSync('spo2_history')) wx.setStorageSync('spo2_history', []);
    if (!wx.getStorageSync('temp_history')) wx.setStorageSync('temp_history', []);
    if (!wx.getStorageSync('alarm_messages')) wx.setStorageSync('alarm_messages', []);
  },

  onUnload() {
    if (this.timer) clearInterval(this.timer);
  },

  async onRefresh() {
    await this.fetchDeviceData(true);
  },

  async fetchDeviceData(showLoading = false) {
    if (this.data.loading) return;
    this.setData({ loading: true });
    if (showLoading) wx.showLoading({ title: '获取数据中', mask: true });

    try {
      const token = await app.getValidToken();
      const { projectId, deviceId, endpoint } = app.globalData;

      // 并发请求：设备影子和设备详情（获取在线状态）
      const [shadowRes, detailRes] = await Promise.all([
        new Promise((resolve, reject) => {
          wx.request({
            url: `https://${endpoint}/v5/iot/${projectId}/devices/${deviceId}/shadow`,
            method: 'GET',
            header: { 'X-Auth-Token': token, 'Content-Type': 'application/json' },
            success: resolve,
            fail: reject
          });
        }),
        new Promise((resolve, reject) => {
          wx.request({
            url: `https://${endpoint}/v5/iot/${projectId}/devices/${deviceId}`,
            method: 'GET',
            header: { 'X-Auth-Token': token, 'Content-Type': 'application/json' },
            success: resolve,
            fail: reject
          });
        })
      ]);

      // 401 刷新 token
      if (shadowRes.statusCode === 401 || detailRes.statusCode === 401) {
        await app.refreshToken();
        return await this.fetchDeviceData(showLoading);
      }

      // 解析在线状态（华为云返回 "ONLINE" 或 "OFFLINE"）
      let deviceOnline = this.data.deviceOnline; // 保持上次状态
      if (detailRes.statusCode === 200 && detailRes.data) {
        const status = detailRes.data.status;
        deviceOnline = (status === 'ONLINE');
      } else {
        console.warn('获取设备详情失败，使用上次在线状态');
      }

      // 解析设备影子
      if (shadowRes.statusCode === 200 && shadowRes.data) {
        this.parseDeviceShadow(shadowRes.data, deviceOnline);
      } else {
        throw new Error(`影子接口错误: ${shadowRes.statusCode}`);
      }

    } catch (err) {
      console.error('获取设备数据失败', err);
      wx.showToast({ title: '数据同步失败', icon: 'none' });
      // 请求异常时标记离线，并保持最后位置
      this.setData({ deviceOnline: false });
      if (this.data.deviceLat && this.data.deviceLng) {
        this.updateMarker(this.data.deviceLng, this.data.deviceLat, false);
      }
    } finally {
      this.setData({ loading: false });
      if (showLoading) wx.hideLoading();
    }
  },

  parseDeviceShadow(shadowData, deviceOnline = true) {
    const shadowArray = shadowData.shadow;
    if (!shadowArray || !Array.isArray(shadowArray)) return;

    let gps = null, health = null, alarm = null;
    for (const item of shadowArray) {
      const properties = item.reported?.properties;
      if (!properties) continue;
      switch (item.service_id) {
        case 'GPS_BD': gps = properties; break;
        case 'Health': health = properties; break;
        case 'Alarm': alarm = properties; break;
      }
    }

    // 处理 GPS
    if (gps && gps.longitude !== undefined && gps.latitude !== undefined) {
      let wgsLng = parseFloat(gps.longitude);
      let wgsLat = parseFloat(gps.latitude);
      if (!isNaN(wgsLng) && !isNaN(wgsLat)) {
        const gcj = coordtransform.wgs84togcj02(wgsLng, wgsLat);
        this.setData({
          longitude: gcj[0],
          latitude: gcj[1],
          deviceOnline
        });
        this.updateMarker(gcj[0], gcj[1], deviceOnline);
      } else {
        // 坐标无效，使用旧坐标并标记离线
        this.setData({ deviceOnline: false });
        if (this.data.deviceLat && this.data.deviceLng) {
          this.updateMarker(this.data.deviceLng, this.data.deviceLat, false);
        }
      }
    } else {
      // 无 GPS 数据，保持旧坐标，按真实在线状态显示
      this.setData({ deviceOnline });
      if (this.data.deviceLat && this.data.deviceLng) {
        this.updateMarker(this.data.deviceLng, this.data.deviceLat, deviceOnline);
      }
    }

    // 健康数据
    let heartRate = '--', spo2 = '--', temp = '--', step = '--';
    if (health) {
      if (health.heart_rate != null) heartRate = health.heart_rate;
      if (health.spo2 != null) spo2 = health.spo2;
      if (health.temp != null) temp = parseFloat(health.temp).toFixed(1);
      if (health.step != null) step = health.step;
    }
    this.setData({ heartRate, spo2, temp, step });

    this.saveHistoryData('heart_rate', heartRate);
    this.saveHistoryData('spo2', spo2);
    this.saveHistoryData('temp', temp);

    // 警报
    let alarmStatus = alarm?.alarm_status || 'Normal';
    this.updateAlarmDisplay(alarmStatus);
    if (alarmStatus !== 'Normal') {
      this.recordAlarmMessage(alarmStatus);
    }

    this.setData({ lastUpdate: new Date().toLocaleTimeString() });
  },

  // 其他方法不变
  recordAlarmMessage(status) {
    let messages = wx.getStorageSync('alarm_messages') || [];
    const lastMsg = messages[messages.length - 1];
    if (lastMsg && lastMsg.type === status) {
      const lastTime = new Date(lastMsg.timestamp).getTime();
      if (Date.now() - lastTime < 60 * 1000) return;
    }
    const message = {
      type: status,
      timestamp: new Date().toISOString(),
      timeStr: new Date().toLocaleString(),
      description: this.getAlarmDescription(status)
    };
    messages.push(message);
    if (messages.length > 200) messages = messages.slice(-200);
    wx.setStorageSync('alarm_messages', messages);
  },

  getAlarmDescription(status) {
    const map = {
      'SOS': '老人触发SOS紧急求救',
      'Fall': '检测到老人跌倒',
      'High_heart_rate': '心率过高，请注意',
      'Low_heart_rate': '心率过低，请注意',
      'Abnormal_temp': '体温异常，请注意',
      'Low_spo2': '血氧饱和度过低'
    };
    return map[status] || '异常警报';
  },

  saveHistoryData(key, value) {
    if (value === '--' || value === null || value === undefined) return;
    const storageKey = `${key}_history`;
    let history = wx.getStorageSync(storageKey) || [];
    const newRecord = {
      value: key === 'temp' ? parseFloat(value) : Number(value),
      timestamp: new Date().toISOString(),
      timeStr: new Date().toLocaleTimeString()
    };
    history.push(newRecord);
    if (history.length > 100) history = history.slice(-100);
    wx.setStorageSync(storageKey, history);
  },

  updateMarker(lng, lat, isOnline = true) {
    const content = isOnline ? '设备位置' : '设备已离线';
    const bgColor = isOnline ? '#ffffff' : '#fff3cd';
    const color = isOnline ? '#333333' : '#856404';

    this.setData({
      markers: [{
        id: 100,
        longitude: lng,
        latitude: lat,
        title: content,
        iconPath: '',
        width: 30,
        height: 30,
        callout: {
          content: content,
          color: color,
          fontSize: 14,
          borderRadius: 10,
          bgColor: bgColor,
          padding: 8,
          display: 'ALWAYS'
        }
      }],
      deviceLng: lng,
      deviceLat: lat
    });
  },

  updateAlarmDisplay(status) {
    let text = '正常', cls = 'normal';
    switch (status) {
      case 'SOS': text = 'SOS 紧急'; cls = 'danger'; break;
      case 'Fall': text = '跌倒警报'; cls = 'danger'; break;
      case 'High_heart_rate': text = '心率过高'; cls = 'danger'; break;
      case 'Low_heart_rate': text = '心率过低'; cls = 'danger'; break;
      case 'Abnormal_temp': text = '体温异常'; cls = 'warning'; break;
      case 'Low_spo2': text = '血氧过低'; cls = 'danger'; break;
      default: text = '正常'; cls = 'normal';
    }
    this.setData({ alarmText: text, alarmClass: cls, alarmStatus: status });
  },

  onMapTap() {
    const { deviceLat, deviceLng } = this.data;
    if (deviceLat && deviceLng) {
      wx.openLocation({
        latitude: deviceLat,
        longitude: deviceLng,
        name: '设备位置',
        address: '老人活动检测设备',
        scale: 18,
        success: () => console.log('导航启动成功'),
        fail: (err) => {
          console.error('导航失败', err);
          if (err.errMsg && err.errMsg.includes('auth deny')) {
            wx.showModal({
              title: '提示',
              content: '需要您的位置权限才能导航，请前往设置开启',
              confirmText: '去设置',
              success: (res) => { if (res.confirm) wx.openSetting(); }
            });
          } else {
            wx.showToast({ title: '导航失败，请稍后重试', icon: 'none' });
          }
        }
      });
    } else {
      wx.showToast({ title: '暂无设备位置信息', icon: 'none' });
    }
  },

  onHeartRateCardTap() {
    wx.navigateTo({ url: '/pages/heartRateStat/heartRateStat' });
  },
  onSpo2CardTap() {
    wx.navigateTo({ url: '/pages/spo2Stat/spo2Stat' });
  },
  onTempCardTap() {
    wx.navigateTo({ url: '/pages/tempStat/tempStat' });
  }
});
// pages/message/message.js
const app = getApp();

Page({
  data: {
    groupedMessages: [],
    hasMessages: false,
    showModal: false,
    addressInput: '',
    scrollToView: ''
  },

  onLoad() {
    this.loadMessages();
  },

  onShow() {
    this.loadMessages();
  },

  loadMessages() {
    const messages = wx.getStorageSync('alarm_messages') || [];
    const grouped = this.groupMessagesByDate(messages);
    this.setData({
      groupedMessages: grouped,
      hasMessages: messages.length > 0
    });
  },

  groupMessagesByDate(messages) {
    const groups = {};
    messages.sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp));
    
    messages.forEach(msg => {
      const date = new Date(msg.timestamp);
      const dateKey = `${date.getFullYear()}-${date.getMonth()+1}-${date.getDate()}`;
      if (!groups[dateKey]) {
        groups[dateKey] = {
          date: dateKey,
          dateDisplay: this.formatDateDisplay(date),
          messages: []
        };
      }
      groups[dateKey].messages.push({
        ...msg,
        timeStr: this.formatTime(date),
        isNormal: msg.type === 'Normal'
      });
    });
    
    return Object.values(groups);
  },

  formatDateDisplay(date) {
    const today = new Date();
    const yesterday = new Date(today);
    yesterday.setDate(yesterday.getDate() - 1);
    
    if (date.toDateString() === today.toDateString()) return '今天';
    if (date.toDateString() === yesterday.toDateString()) return '昨天';
    return `${date.getMonth()+1}月${date.getDate()}日`;
  },

  formatTime(date) {
    return `${date.getHours().toString().padStart(2,'0')}:${date.getMinutes().toString().padStart(2,'0')}`;
  },

  onAddAddress() {
    // 先尝试获取已存储的地址
    const savedAddr = wx.getStorageSync('family_address') || '';
    this.setData({ showModal: true, addressInput: savedAddr });
  },

  hideModal() {
    this.setData({ showModal: false });
  },

  onAddressInput(e) {
    this.setData({ addressInput: e.detail.value });
  },

  async submitAddress() {
    const address = this.data.addressInput.trim();
    if (!address) {
      wx.showToast({ title: '请输入地址', icon: 'none' });
      return;
    }
  
    wx.showLoading({ title: '下发地址中...' });
  
    try {
      const token = await app.getValidToken();
      const { projectId, deviceId, endpoint } = app.globalData;
  
      const commandBody = {
        shadow: [{
          service_id: 'Family_addr',
          desired: {
            addr: address          // 直接写属性
          }
        }]
      };
  
      const url = `https://${endpoint}/v5/iot/${projectId}/devices/${deviceId}/shadow`;
  
      const res = await new Promise((resolve, reject) => {
        wx.request({
          url: url,
          method: 'PUT',           
          header: {
            'X-Auth-Token': token,
            'Content-Type': 'application/json'
          },
          data: commandBody,
          success: resolve,
          fail: reject
        });
      });
  
      if (res.statusCode === 200) {
        wx.setStorageSync('family_address', address);
        wx.hideLoading();
        wx.showToast({ title: '地址已下发', icon: 'success' });
        this.hideModal();
      } else {
        throw new Error(`下发失败: ${res.statusCode}`);
      }
    } catch (err) {
      wx.hideLoading();
      console.error('地址下发失败', err);
      wx.showToast({ title: '下发失败，请重试', icon: 'none' });
    }
  }
});
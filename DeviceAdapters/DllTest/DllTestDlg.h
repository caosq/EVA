
// DllTestDlg.h : ͷ�ļ�
//
#include "MMDevice.h"

#pragma once

#define NET_DVR_API  extern "C"__declspec(dllimport)

NET_DVR_API void __stdcall InitializeModuleData();
NET_DVR_API  MM::Device*  __stdcall    CreateDevice(const char* deviceName);

// CDllTestDlg �Ի���
class CDllTestDlg : public CDialogEx
{
// ����
public:
	CDllTestDlg(CWnd* pParent = NULL);	// ��׼���캯��

// �Ի�������
	enum { IDD = IDD_DLLTEST_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV ֧��


// ʵ��
protected:
	HICON m_hIcon;

	// ���ɵ���Ϣӳ�亯��
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedButton1();
	const char* deviceName;


};

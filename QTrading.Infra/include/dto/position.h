#pragma once

#include <string>

namespace QTrading::dto {
	struct PositionDto {
		std::string symbol;
        double positionAmount;    // �ܦ�ƶq�]���Ƭ��h�Y�A�t�Ƭ����Y�^
        double entryPrice;        // �}�ܻ���
        double markPrice;         // ��e�аO����
        double unrealizedPnL;     // ����{����
        double leverage;          // ���쭿��
        bool isolated;            // �O�_���v�ܼҦ�
        std::string positionSide; // �ܦ��V�A�Ҧp "BOTH", "LONG", "SHORT"
	};
}

"""Chain list: drag to reorder, checkbox = module active (unchecked = bypassed)."""

from __future__ import annotations

from collections.abc import Callable

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QAbstractItemView, QListWidget, QListWidgetItem

from mastermind.dsp.chain import MasteringChain

TYPE_ROLE = Qt.ItemDataRole.UserRole


class ChainListWidget(QListWidget):
    def __init__(
        self,
        on_reorder: Callable[[list[str]], None],
        on_bypass: Callable[[str, bool], None],
    ):
        super().__init__()
        self._on_reorder = on_reorder
        self._on_bypass = on_bypass
        self._pinned: set[str] = set()
        self._syncing = False
        self.setDragDropMode(QAbstractItemView.DragDropMode.InternalMove)
        self.setDefaultDropAction(Qt.DropAction.MoveAction)
        self.model().rowsMoved.connect(self._rows_moved)
        self.itemChanged.connect(self._item_changed)

    def set_chain(self, chain: MasteringChain) -> None:
        self._syncing = True
        self.clear()
        self._pinned = set()
        for slot in chain.slots:
            p = slot.processor
            item = QListWidgetItem(p.display_name)
            item.setData(TYPE_ROLE, p.type_id)
            flags = item.flags() | Qt.ItemFlag.ItemIsUserCheckable
            if p.pinned_last:
                self._pinned.add(p.type_id)
                flags &= ~Qt.ItemFlag.ItemIsDragEnabled
            item.setFlags(flags)
            item.setCheckState(Qt.CheckState.Unchecked if slot.bypass else Qt.CheckState.Checked)
            self.addItem(item)
        self._syncing = False

    def order(self) -> list[str]:
        return [self.item(i).data(TYPE_ROLE) for i in range(self.count())]

    def _rows_moved(self, *args) -> None:
        if self._syncing:
            return
        order = self.order()
        # Pinned modules (dither) always stay last.
        fixed = [t for t in order if t not in self._pinned] + [t for t in order if t in self._pinned]
        if fixed != order:
            self._syncing = True
            for t in self._pinned:
                for i in range(self.count()):
                    if self.item(i).data(TYPE_ROLE) == t:
                        self.addItem(self.takeItem(i))
                        break
            self._syncing = False
        self._on_reorder(self.order())

    def _item_changed(self, item: QListWidgetItem) -> None:
        if self._syncing:
            return
        self._on_bypass(item.data(TYPE_ROLE), item.checkState() != Qt.CheckState.Checked)

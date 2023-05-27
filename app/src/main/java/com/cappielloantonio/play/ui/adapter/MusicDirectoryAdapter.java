package com.cappielloantonio.play.ui.adapter;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.media3.common.util.UnstableApi;
import androidx.recyclerview.widget.RecyclerView;

import com.cappielloantonio.play.databinding.ItemLibraryMusicDirectoryBinding;
import com.cappielloantonio.play.glide.CustomGlideRequest;
import com.cappielloantonio.play.interfaces.ClickCallback;
import com.cappielloantonio.play.subsonic.models.Child;
import com.cappielloantonio.play.util.Constants;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

@UnstableApi
public class MusicDirectoryAdapter extends RecyclerView.Adapter<MusicDirectoryAdapter.ViewHolder> {
    private final ClickCallback click;

    private List<Child> children;

    public MusicDirectoryAdapter(ClickCallback click) {
        this.click = click;
        this.children = Collections.emptyList();
    }

    @NonNull
    @Override
    public ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        ItemLibraryMusicDirectoryBinding view = ItemLibraryMusicDirectoryBinding.inflate(LayoutInflater.from(parent.getContext()), parent, false);
        return new ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(ViewHolder holder, int position) {
        Child child = children.get(position);

        holder.item.musicDirectoryTitleTextView.setText(child.getTitle());

        CustomGlideRequest.Builder
                .from(holder.itemView.getContext(), child.getCoverArtId())
                .build()
                .into(holder.item.musicDirectoryCoverImageView);
    }

    @Override
    public int getItemCount() {
        return children.size();
    }

    public void setItems(List<Child> children) {
        this.children = children != null ? children : Collections.emptyList();
        notifyDataSetChanged();
    }

    public class ViewHolder extends RecyclerView.ViewHolder {
        ItemLibraryMusicDirectoryBinding item;

        ViewHolder(ItemLibraryMusicDirectoryBinding item) {
            super(item.getRoot());

            this.item = item;

            item.musicDirectoryTitleTextView.setSelected(true);

            itemView.setOnClickListener(v -> onClick());
            itemView.setOnLongClickListener(v -> onLongClick());

            item.musicDirectoryMoreButton.setOnClickListener(v -> onLongClick());
        }

        public void onClick() {
            Bundle bundle = new Bundle();

            if (children.get(getBindingAdapterPosition()).isDir()) {
                bundle.putParcelable(Constants.MUSIC_DIRECTORY_OBJECT, children.get(getBindingAdapterPosition()));
                click.onMusicDirectoryClick(bundle);
            } else {
                bundle.putParcelableArrayList(Constants.TRACKS_OBJECT, new ArrayList<>(children));
                bundle.putInt(Constants.ITEM_POSITION, getBindingAdapterPosition());
                click.onMediaClick(bundle);
            }
        }

        private boolean onLongClick() {
            Bundle bundle = new Bundle();
            bundle.putParcelable(Constants.MUSIC_DIRECTORY_OBJECT, children.get(getBindingAdapterPosition()));

            click.onMusicDirectoryLongClick(bundle);

            return true;
        }
    }
}
